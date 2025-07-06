Import("env")
import os
import shutil
from pathlib import Path

def create_littlefs(source, target, env):
    print("Creating LittleFS image from data directory")
    data_dir = os.path.join(env.get("PROJECT_DIR"), "data")
    if not os.path.exists(data_dir):
        print(f"Data directory {data_dir} doesn't exist, creating it")
        os.makedirs(data_dir)
    else:
        print(f"Data directory {data_dir} exists")

env.AddPreAction("buildfs", create_littlefs) 