#pragma once

// ============================================================
//  THE LABEL EDITOR
//
//  The label as it will print, on the display, next to its lines. A tap on a
//  line opens the keyboard for it. Three ways in:
//   - a spool's label, from the More info card: starts from the spool and
//     the template, changes hold for this print only;
//   - the template, from the printer screen: which facts a spool's label
//     carries and its last line, shown on a sample spool, saved at once;
//   - a free label, from the printer screen: any text, no spool.
//  Printing runs from the loop (labelEditLoop), the BLE session blocks.
// ============================================================

// Asks for the spool on the scale; the editor opens from the loop, after the
// backend was asked for the label's date.
void labelEditRequestSpool();
void labelEditOpenTemplate();
void labelEditOpenFree();

// From the printer screen's deferred actions.
void labelEditLoop();

// For hideAllOverlays().
void labelEditClose();
