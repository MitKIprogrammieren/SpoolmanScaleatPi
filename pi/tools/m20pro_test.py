#!/usr/bin/env python3
"""Probe a Phomemo label printer (made for the M20Pro) from the Raspberry Pi.

The scale knows the M220/M110/M100 byte protocol. This tool finds out whether
the M20Pro answers to the same bytes, before the scale gets a real driver.

  sudo apt install python3-bleak python3-pil
  python3 m20pro_test.py scan                 # find the printer (BLE)
  python3 m20pro_test.py info  AA:BB:...      # list its Bluetooth services
  python3 m20pro_test.py print AA:BB:...      # test label, M220 bytes, 300 dpi
  python3 m20pro_test.py print AA:BB:... --proto m110
  python3 m20pro_test.py print AA:BB:... --rfcomm   # classic Bluetooth instead

Every step prints what it sent and what the printer answered. Copy the whole
output into the chat.
"""
import argparse
import asyncio
import socket
import sys
import time

SERVICE = "0000ff00-0000-1000-8000-00805f9b34fb"
WRITE = "0000ff02-0000-1000-8000-00805f9b34fb"
STATUS = "0000ff03-0000-1000-8000-00805f9b34fb"

M220 = {
    "head": [b"\x1b\x40", b"\x1b\x37\x07\x64\x64"],
    "foot": [b"\x1b\x4a\x20"],
}
M110 = {
    "head": [b"\x1b\x4e\x0d\x05", b"\x1b\x4e\x04\x0a", b"\x1f\x11\x0a"],
    "foot": [b"\x1f\xf0\x05\x00", b"\x1f\xf0\x03\x00"],
}
# Questions many Phomemo models answer: firmware, battery, paper state.
QUERIES = [b"\x1f\x11\x07", b"\x1f\x11\x08", b"\x1f\x11\x09", b"\x1f\x11\x11", b"\x1f\x11\x12"]


def hexs(b):
    return " ".join(f"{x:02x}" for x in b)


def dots(mm, dpi):
    return int(round(mm * dpi / 25.4))


def test_image(width, height):
    """1-bit rows, MSB first, 1 = black: a frame, a cross and a title."""
    bpr = (width + 7) // 8
    try:
        from PIL import Image, ImageDraw, ImageFont
        im = Image.new("1", (width, height), 1)
        d = ImageDraw.Draw(im)
        d.rectangle([2, 2, width - 3, height - 3], outline=0, width=4)
        d.line([0, 0, width - 1, height - 1], fill=0, width=2)
        d.line([0, height - 1, width - 1, 0], fill=0, width=2)
        size = max(16, min(height // 7, width // 10))
        try:
            font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", size)
        except OSError:
            font = ImageFont.load_default()
        for i, t in enumerate(["FilamentLager", f"{width}x{height}"]):
            box = d.textbbox((0, 0), t, font=font)
            w = box[2] - box[0]
            y = height // 3 + i * (size + 8) - size // 2
            d.rectangle([(width - w) // 2 - 6, y - 4, (width + w) // 2 + 6, y + size + 4], fill=1)
            d.text(((width - w) // 2, y), t, fill=0, font=font)
        rows = bytearray()
        px = im.load()
        for y in range(height):
            for bx in range(bpr):
                v = 0
                for bit in range(8):
                    x = bx * 8 + bit
                    if x < width and px[x, y] == 0:
                        v |= 0x80 >> bit
                rows.append(v)
        return bytes(rows)
    except ImportError:
        rows = bytearray()
        for y in range(height):
            for bx in range(bpr):
                edge = y < 4 or y >= height - 4 or bx == 0 or bx == bpr - 1
                rows.append(0xFF if edge or (y // 16 + bx) % 4 == 0 else 0x00)
        return bytes(rows)


def job(args):
    width = dots(args.width, args.dpi) // 8 * 8
    height = dots(args.height, args.dpi)
    img = test_image(width, height)
    bpr = width // 8
    raster = bytes([0x1d, 0x76, 0x30, 0x00, bpr & 0xFF, bpr >> 8, height & 0xFF, height >> 8])
    p = M220 if args.proto == "m220" else M110
    print(f"Label {args.width}x{args.height} mm at {args.dpi} dpi = {width}x{height} dots, "
          f"{len(img)} bytes, protocol {args.proto}")
    return p["head"] + [raster, img] + p["foot"]


# ---------------------------------------------------------------- BLE (bleak)
def need_bleak():
    try:
        import bleak  # noqa: F401
    except ImportError:
        sys.exit("bleak is missing: sudo apt install python3-bleak")


async def scan(_args):
    need_bleak()
    from bleak import BleakScanner
    print("Scanning 10 s ...")
    found = await BleakScanner.discover(timeout=10, return_adv=True)
    for addr, (dev, adv) in sorted(found.items(), key=lambda kv: -kv[1][1].rssi):
        name = dev.name or adv.local_name or "?"
        mark = "  <-- Phomemo?" if any(k in name.upper() for k in ("M20", "PHOMEMO", "Q")) else ""
        print(f"{addr}  {adv.rssi:4d} dBm  {name}  {list(adv.service_uuids)}{mark}")
    print("Not listed? Then the printer may only speak classic Bluetooth: bluetoothctl scan on")


async def info(args):
    need_bleak()
    from bleak import BleakClient
    async with BleakClient(args.address, timeout=20) as c:
        print(f"Connected, MTU {c.mtu_size}")
        for s in c.services:
            print(f"service {s.uuid}  {s.description}")
            for ch in s.characteristics:
                print(f"   char {ch.uuid}  [{','.join(ch.properties)}]  handle {ch.handle}")


async def ble_print(args):
    need_bleak()
    from bleak import BleakClient
    answers = []

    def on_note(_h, data):
        answers.append(bytes(data))
        print(f"   <- {hexs(data)}")

    async with BleakClient(args.address, timeout=20) as c:
        chars = {ch.uuid: ch for s in c.services for ch in s.characteristics}
        wr = args.char or (WRITE if WRITE in chars else None)
        if not wr:
            sys.exit("No ff02 write characteristic; run 'info' and pass one with --char")
        for ch in chars.values():
            if "notify" in ch.properties:
                try:
                    await c.start_notify(ch.uuid, on_note)
                    print(f"listening on {ch.uuid}")
                except Exception as e:  # noqa: BLE001
                    print(f"cannot listen on {ch.uuid}: {e}")
        chunk = max(20, min(128, c.mtu_size - 3))
        resp = "write" in chars[wr].properties and "write-without-response" not in chars[wr].properties
        print(f"MTU {c.mtu_size}, chunks of {chunk}, write with response: {resp}")

        print("Questions:")
        for q in QUERIES:
            print(f"   -> {hexs(q)}")
            await c.write_gatt_char(wr, q, response=resp)
            await asyncio.sleep(0.4)
        if args.only_query:
            return
        print("Printing:")
        t0 = time.time()
        for blk in job(args):
            if len(blk) <= 16:
                print(f"   -> {hexs(blk)}")
            for i in range(0, len(blk), chunk):
                await c.write_gatt_char(wr, blk[i:i + chunk], response=resp)
                await asyncio.sleep(args.gap / 1000)
        print(f"sent in {time.time() - t0:.1f} s, waiting 10 s for answers ...")
        await asyncio.sleep(10)
    print("Done. Did a label come out? Is the picture complete, cut off or noise?")


# ---------------------------------------------------- classic Bluetooth (RFCOMM)
def rfcomm_print(args):
    s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
    s.settimeout(15)
    print(f"RFCOMM connect {args.address} channel {args.channel} ...")
    s.connect((args.address, args.channel))
    s.settimeout(0.5)

    def drain():
        try:
            while True:
                d = s.recv(256)
                if not d:
                    break
                print(f"   <- {hexs(d)}")
        except (socket.timeout, BlockingIOError):
            pass

    print("Questions:")
    for q in QUERIES:
        print(f"   -> {hexs(q)}")
        s.sendall(q)
        time.sleep(0.4)
        drain()
    if not args.only_query:
        print("Printing:")
        for blk in job(args):
            if len(blk) <= 16:
                print(f"   -> {hexs(blk)}")
            s.sendall(blk)
        time.sleep(5)
        drain()
    s.close()
    print("Done. Did a label come out? Is the picture complete, cut off or noise?")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["scan", "info", "print", "query"])
    ap.add_argument("address", nargs="?")
    ap.add_argument("--proto", choices=["m220", "m110"], default="m220")
    ap.add_argument("--dpi", type=int, default=300)
    ap.add_argument("--width", type=float, default=40, help="label width in mm")
    ap.add_argument("--height", type=float, default=30, help="label height in mm")
    ap.add_argument("--char", help="write characteristic UUID if not ff02")
    ap.add_argument("--gap", type=int, default=20, help="ms between BLE chunks")
    ap.add_argument("--rfcomm", action="store_true", help="classic Bluetooth instead of BLE")
    ap.add_argument("--channel", type=int, default=1)
    args = ap.parse_args()
    args.only_query = args.cmd == "query"
    if args.cmd != "scan" and not args.address:
        ap.error("address needed (from 'scan')")
    if args.rfcomm and args.cmd in ("print", "query"):
        return rfcomm_print(args)
    run = {"scan": scan, "info": info, "print": ble_print, "query": ble_print}[args.cmd]
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
