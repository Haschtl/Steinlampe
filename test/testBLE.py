#!/usr/bin/env python3
# scan_and_wake.py
import asyncio
from bleak import BleakScanner, BleakClient

SERVICE_UUID = "d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b"
CHAR_UUID = "4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7"
STATUS_UUID = "c5ad78b6-9b77-4a96-9a42-8e6e9a40c123"
TARGET_NAME = "Quarzlampe"          # ggf. anpassen

async def main():
    print("Scanning...")
    devices = await BleakScanner.discover(timeout=5.0)
    target = None
    for d in devices:
        uuids = [u.lower() for u in d.details.get("uuids", [])]
        if d.name == TARGET_NAME or SERVICE_UUID.lower() in uuids:
            target = d
            break
    if not target:
        print("Kein Gerät gefunden.")
        return
    print(f"Verbinde zu {target.address} ({target.name})")

    def on_notify(_, data: bytearray):
        try:
            text = data.decode(errors="ignore")
        except Exception:
            text = repr(data)
        print(f"< {text}")

    async with BleakClient(target.address) as client:
        try:
            await client.start_notify(CHAR_UUID, on_notify)
        except Exception as e:
            print("Hinweis: command notify start fehlgeschlagen:", e)
        try:
            await client.start_notify(STATUS_UUID, on_notify)
        except Exception:
            pass
        print("Verbunden. Tippe Befehle (z.B. 'wake 180'), leer oder Ctrl+C zum Beenden.")
        while True:
            try:
                line = input("cmd> ").strip()
            except (EOFError, KeyboardInterrupt):
                print("\nBeende.")
                break
            if not line:
                print("Beende.")
                break
            try:
                await client.write_gatt_char(CHAR_UUID, line.encode("utf-8"), response=False)
                print(f"Gesendet: {line}")
            except Exception as e:
                print("Fehler beim Senden:", e)

if __name__ == "__main__":
    asyncio.run(main())
