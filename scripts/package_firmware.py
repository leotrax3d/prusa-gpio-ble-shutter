#!/usr/bin/env python3
"""After pio run: generate a browser-installable full image and verify application identity."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,shutil
root=Path(__file__).resolve().parents[1]
a=argparse.ArgumentParser();a.add_argument('--core-dir',default=os.environ.get('PLATFORMIO_CORE_DIR',str(Path.home()/'.platformio')));args=a.parse_args()
core=Path(args.core_dir);build=root/'.pio/build/esp32dev';out=root/'site/firmware';out.mkdir(parents=True,exist_ok=True)
app=build/'firmware.bin';data=app.read_bytes()
assert data[0]==0xe9 and data[12:14]==b'\0\0' and data[32:36]==bytes.fromhex('3254cdab'),'Not a classic ESP32 application image'
assert len(data)<=0x1e0000,'Application exceeds OTA partition'
version='2.0.2'
subprocess.run([sys.executable,str(core/'packages/tool-esptoolpy/esptool.py'),'--chip','esp32','merge_bin','--flash_mode','dio','--flash_freq','40m','--flash_size','4MB','-o',str(out/'merged.bin'),'0x1000',str(build/'bootloader.bin'),'0x8000',str(build/'partitions.bin'),'0xe000',str(core/'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'),'0x10000',str(app)],check=True)
shutil.copyfile(app,out/'firmware.bin')
manifest={'name':'Prusa Shutter','version':version,'new_install_prompt_erase':True,'builds':[{'chipFamily':'ESP32','parts':[{'path':'merged.bin','offset':0}]}]}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((out/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in ['merged.bin','firmware.bin']))
print('Packaged firmware '+version)
