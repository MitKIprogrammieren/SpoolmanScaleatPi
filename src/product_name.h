#pragma once
// The product name on the screen, in the browser and towards the backends.
// A build can set its own, in two parts for the two-tone wordmark
// (the Raspberry Pi build: "Filament" + "Lager").
#ifndef FW_PRODUCT_NAME_1
#define FW_PRODUCT_NAME_1 "Spoolman"
#endif
#ifndef FW_PRODUCT_NAME_2
#define FW_PRODUCT_NAME_2 "Scale"
#endif
#define FW_PRODUCT_NAME FW_PRODUCT_NAME_1 FW_PRODUCT_NAME_2
