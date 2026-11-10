"""UI contract tests with explicit mock device responses; no hardware claims."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import copy,json,threading,http.server,functools,os,shutil
root=Path(__file__).resolve().parents[1]
config=dict(schema=2,mode=0,key=0,pin=27,activeHigh=True,pullup=True,disarmOnDisconnect=True,debounce=30,cooldown=1000,hold=80,delay=0,every=1,burst=1,burstGap=1200,interval=5000,startDelay=3000,maxFrames=0,autoStop=0)
servo=dict(pin=26,degrees=270,minUs=1000,maxUs=2000,stepUs=10,commandPin=33,automation=False,rules=[])
network=dict(ap='Prusa-Shutter',ssid='',hostname='prusa-shutter',bleName='Prusa Shutter',channel=1)
status=dict(version='2.0.0',connected=True,ready=True,armed=False,pending=False,held=False,level=0,pin=27,sent=0,session=0,pulses=0,skipped=0,filtered=0,remaining=0,mode=0,nextMs=0,uptime=30,freeHeap=120000,minHeap=110000,flashSize=4194304,appSize=1700000,otaSpace=1966080,wifiConnected=False,ip='',apIP='192.168.4.1',clients=1,rssi=0,bonds=1,resetReason=1,rebootRequired=False,servo=dict(enabled=False,targetUs=0,currentUs=0))
class Handler(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(root)))
threading.Thread(target=server.serve_forever,daemon=True).start()
base=f'http://127.0.0.1:{server.server_port}'
try:
 with sync_playwright() as p:
  browser=p.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH') or shutil.which('chromium'),headless=True,args=['--no-sandbox'])
  for width in [390,1440]:
   c=copy.deepcopy(config);sv=copy.deepcopy(servo);s=copy.deepcopy(status);posts=[];errors=[]
   page=browser.new_page(viewport={'width':width,'height':900})
   page.on('pageerror',lambda e:errors.append(str(e)))
   def api(route):
    path=route.request.url.split('/api/')[1]
    if route.request.method=='POST':
     assert route.request.headers.get('x-shutter')=='1'
     d=route.request.post_data_json;posts.append((path,d))
     if path=='config':c.update(d);s['mode']=c['mode'];s['armed']=False
     elif path=='servo/config':sv.update(d);s['servo']['enabled']=False
     elif path=='action':
      if d['action']=='start':s['armed']=True
      if d['action']=='stop':s['armed']=False
      if d['action']=='test':s['sent']+=1
     elif path=='servo/action':s['servo']['enabled']=d['action']!='disable'
     route.fulfill(json={'message':'OK'});return
    data={'config':c,'status':s,'network':network,'servo/config':sv,'logs':{'events':[{'ms':1000,'text':'System bereit.'},{'ms':2000,'text':'<img src=x onerror=alert(1)>'}]}}
    route.fulfill(json=data[path])
   page.route('**/api/**',api)
   page.goto(base+'/web/dashboard.html')
   page.get_by_text('Bluetooth bereit',exact=True).wait_for()
   page.locator('[data-action=test]').click();page.wait_for_function("document.querySelector('#sent').textContent==='1'")
   page.locator('nav [data-tab=capture]').click()
   page.locator('[data-preset=fifth]').click();page.locator('#capture [data-save]').click()
   page.wait_for_function("document.querySelector('#capture .saveState').textContent==='Alles gespeichert'")
   assert c['every']==5
   page.locator('nav [data-tab=slicer]').click();assert 'jeder 5.' in page.locator('#gcodeSummary').inner_text()
   page.locator('#includeServo').check();page.locator('#addRule').click()
   page.locator('[data-rule=when]').select_option('layerNumber');page.locator('[data-rule=angle]').fill('200');page.locator('[data-rule=layer]').fill('10');page.locator('#servoAutomation').check();page.locator('#savePlan').click()
   page.wait_for_function("document.querySelector('#planState').textContent==='Aktionsplan gespeichert'")
   assert sv['rules'][0]['angle']==200 and sv['automation']
   code=page.locator('#layerCode').inner_text();assert '{if layer_num == 10}' in code and 'G4 P200' in code and 'M264 P6 B1' in code and 'M264 P7 B1' in code
   assert 'M262 P6 B0' in page.locator('#startCode').inner_text()
   page.screenshot(path=str(root/f'docs/configurator-{width}.png'),full_page=True)
   page.locator('#includePhoto').uncheck();assert 'M264 P7' not in page.locator('#layerCode').inner_text()
   page.locator('nav [data-tab=servo]').click();page.once('dialog',lambda d:d.accept());page.locator('#enableServo').click();page.wait_for_function("document.querySelector('#servoState').textContent.includes('freigegeben')")
   page.locator('#servoAngle').fill('180');page.locator('#moveServo').click();page.wait_for_timeout(150)
   assert any(path=='servo/action' and d['action']=='move' and d['angle']==180 for path,d in posts)
   page.locator('nav [data-tab=diagnostics]').click();page.get_by_text('System bereit.',exact=True).wait_for();assert page.locator('#logs img').count()==0
   page.locator('#theme').click();assert page.locator('html').get_attribute('data-theme')=='light'
   for name in ['overview','capture','input','slicer','servo','network','diagnostics','system']:
    page.locator(f'nav [data-tab={name}]').click()
    assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),(width,name)
   page.locator('nav [data-tab=overview]').click();page.screenshot(path=str(root/f'docs/dashboard-{width}.png'),full_page=True)
   assert not errors,errors
   page.close()
  # Installer tested with a mock custom element; a separate smoke check loads the real CDN module.
  page=browser.new_page(viewport={'width':390,'height':844});errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
  page.add_init_script("Object.defineProperty(navigator,'serial',{value:{requestPort:async()=>{throw new Error('No hardware')}}});")
  page.route('**/vendor/esp-web-tools-10.1.0/install-button.js',lambda r:r.fulfill(body="customElements.define('esp-web-install-button',class extends HTMLElement{});",content_type='application/javascript',headers={'access-control-allow-origin':'*'}))
  page.goto(base+'/site/index.html');page.get_by_text('Bereit · USB-Zugriff wird erst nach deinem Klick angefragt.',exact=True).wait_for()
  assert not page.locator('#installButton').is_disabled()
  assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
  page.screenshot(path=str(root/'docs/installer-mobile.png'),full_page=True);assert not errors,errors;page.close()
  page=browser.new_page();page.add_init_script("delete Navigator.prototype.serial;delete navigator.serial;")
  page.goto(base+'/site/index.html');page.get_by_text('Dieser Browser unterstützt Web Serial nicht. Desktop-Chrome oder Edge verwenden.',exact=True).wait_for();assert page.locator('#installButton').is_disabled();page.close()
  browser.close()
 print('PASS: desktop/mobile dashboard, presets, config save, servo action planner, generated G-code, manual servo, log escaping, all tab layouts, installer ready/unsupported states. Device API and installer element simulated.')
finally:server.shutdown()
