# The Workshop — public site

A static site anyone can visit to build these projects with **their own**
Arduino. The code goes from the page straight onto their board over WebSerial:
no server, no download, no Arduino IDE.

## Deploying

```bash
vercel login
vercel --prod
```

Run both from this folder. It's a static site with no build step, so Vercel
serves it as-is.

HTTPS matters here and Vercel gives it automatically — browsers refuse USB
serial access on a plain `http://` origin, so this genuinely cannot be hosted
without TLS.

## How the flashing works

An Arduino Uno's bootloader (optiboot) speaks **STK500v1**, which is short
enough to implement directly in `avr.js`:

1. Pulse DTR/RTS, which is what the auto-reset circuit on every Uno listens for
2. Sync — repeatedly, because optiboot only listens for about half a second
3. Enter programming mode
4. For each 128-byte page: load the address, write the page
5. Leave programming mode

The visitor picks their device in the browser's own permission dialog. Nothing
is uploaded anywhere; the hex file is fetched from this site and written
locally.

### Which boards work

| Board | Flash from browser | Why |
| --- | --- | --- |
| Uno | **yes** | STK500v1, implemented in `avr.js` |
| Mega 2560 | not yet | STK500v2 — different framing, needs its own implementation |
| NodeMCU / ESP8266 | not yet | SLIP-framed esptool protocol; `esptool-js` would do it |

Both unsupported boards say so plainly on their project page and offer the
firmware for download instead. Nothing silently fails.

### Which browsers work

Chrome, Edge and Opera on a desktop. Firefox and Safari have not implemented
WebSerial, and no mobile browser has. The page detects this and says so rather
than presenting a button that cannot work.

## The Intel HEX parser

`parseHex()` validates as it goes, because a corrupted download that reaches
the chip is much worse than one that fails early. It checks every record's
length, rejects non-hex characters, and verifies the checksum.

That last one had a real bug worth remembering: reading past the end of a
truncated line gives `parseInt('') === NaN`, and `NaN & 0xff` is `0` — so a
length-corrupted line passed the checksum untouched. The line length is now
validated first, and `NaN` is caught explicitly. Three corruption cases are
covered: inflated length byte, flipped data byte, truncated line.

## Keeping the firmware current

`firmware/` holds binaries built from the project repos. After changing a
sketch, rebuild and copy it across:

```powershell
Copy-Item ..\temp-sensor\.build\temp_serial\temp_serial.ino.hex firmware\thermometer.hex
```

Parsed sizes should match what `flash.ps1` reported when it built them — 7762
bytes for the thermometer, 8096 for the gas detector, and so on. If they don't,
something copied the wrong file.

## What is deliberately not here

The project dashboards. They read USB serial ports on a specific PC and can
reflash hardware — none of that belongs on a public site, and none of it would
work in a datacentre anyway. This site gives visitors the wiring, the code, and
a way to put that code on their own board.
