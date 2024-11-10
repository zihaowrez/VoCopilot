from scipy.io.wavfile import write
from scipy.signal import resample
import numpy as np
import os
import time
import subprocess

def save_to_wav(filepath):
    """Save byte buffer to a WAV file using scipy.io.wavfile."""
    with open(filepath, 'rb') as file:
      buffer = bytearray(file.read())
      sample_rate = 16000  # Define the sample rate

      # Convert byte buffer to numpy array
      audio_data = np.frombuffer(buffer, dtype=np.uint8).astype(np.int8) * 256
      audio = resample(audio_data, round(len(audio_data) * float(16000) / 8000))

      # Save as a WAV file
      write(f'{os.path.splitext(os.path.basename(filepath))[0]}.wav', sample_rate, audio.astype(np. int16))

def main(directory):
    # Get the current time
    current_time = time.time()
    
    # Iterate over the files in the specified directory
    for filename in os.listdir(directory):
        # Check if the file is a .bin file
        if filename.endswith(".bin"):
            file_path = os.path.join(directory, filename)
            
            # Get the file's last modified time
            file_mod_time = os.path.getmtime(file_path)
            
            # If the file was modified more than 10 seconds ago
            if current_time - file_mod_time > 10:
                # Call the function f with the file path
                save_to_wav(file_path)
                subprocess.run(["../whisper-cpp/main.exe", f"../EdgeDevice/{os.path.splitext(os.path.basename(filename))[0]}.wav", "-otxt", "-nt"], cwd="../whisper-cpp/")
                
                # Delete the file
                # os.remove(file_path)
                print(f"Processed: {filename}")

main(".")

