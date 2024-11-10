import asyncio
from bleak import BleakClient, BleakScanner
from scipy.io.wavfile import write
from scipy.signal import resample
import numpy as np
import os

DATA_CHARACTERISTIC_UUID = "2A6E"
ID_CHARACTERISTIC_UUID = "2A7E"
audio_buffer = bytearray()
audio_id = -1

def id_notification_handler(sender, data):
    global audio_id, audio_buffer
    new_id = int.from_bytes(data, byteorder='little')
    if new_id >= 0x80000000:
        new_id -= 0x100000000
    print(f'new_id is {new_id}, audio_id is {audio_id}')

    if new_id == audio_id:
        return
    else:
        if audio_id != -1:
            mode = 'ab' if os.path.exists(f'test_{audio_id}.bin') else 'wb'
            with open(f'test_{audio_id}.bin', mode) as file:
                file.write(audio_buffer)
                file.close()
        if new_id != -1:
            audio_buffer = bytearray()

    audio_id = new_id

def data_notification_handler(sender, data):
    """Notification handler to append data to byte buffer."""
    global audio_buffer

    if audio_id != -1:
        audio_buffer.extend(data)  # Append received data to byte buffer



async def read_from_ble(device_address):
    async with BleakClient(device_address, timeout=30) as client:
        print(f"Connected to {device_address}")
        if client.is_connected:
            await client.start_notify(DATA_CHARACTERISTIC_UUID, data_notification_handler)
            await client.start_notify(ID_CHARACTERISTIC_UUID, id_notification_handler)
            try:
                # Keep the connection alive indefinitely
                while True:
                    await asyncio.sleep(1)  # Adjust the sleep duration as needed
                    if not client.is_connected:
                        break
            except asyncio.CancelledError:
                # Stop notifications on cancellation
                await client.stop_notify(DATA_CHARACTERISTIC_UUID)
                await client.stop_notify(ID_CHARACTERISTIC_UUID)
                print("Notification stopped and connection closed.")
        
async def main():
    while True:
        devices = await BleakScanner.discover()
        for device in devices:
            print(device.name)
            if device.name == "ArduinoBLE":
                print(f"Found Arduino: {device.address}")
                await read_from_ble(device.address)
                continue

# Run the main function
asyncio.run(main())
