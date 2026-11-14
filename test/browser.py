"""UI contract tests with explicit mock device responses; no hardware claims."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import copy,json,threading,http.server,functools,os,shutil
from fixtures import config,network,status
root=Path(__file__).resolve().parents[1]
def navigate(page,name):
 if page.viewport_size['width']<=650:page.locator('#pageSelect').select_option(name)
 else:page.locator(f'nav [data-tab={name}]').click()
 page.wait_for_function("id=>!document.getElementById(id).hidden",arg=name)
 assert page.locator('#pageSelect').input_value()==name
 assert page.url.endswith('#'+name)

class Handler(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(root)))
threading.Thread(target=server.serve_forever,daemon=True).start()
base=f'http://127.0.0.1:{server.server_port}'
try:
 with sync_playwright() as p:
  browser=p.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH') or shutil.which('chromium'),headless=True,args=['--no-sandbox'])
  for width in [390,1440]:
   c=copy.deepcopy(config);s=copy.deepcopy(status);posts=[];errors=[];rc=dict(configured=False,input=32,output=25,debounce=30,pulse=1000,cooldown=2000)
   page=browser.new_page(viewport={'width':width,'height':900})
   page.on('pageerror',lambda e:errors.append(str(e)))
   def api(route):
    path=route.request.url.split('/api/')[1].split('?')[0]
    if route.request.method=='POST':
     assert route.request.headers.get('x-shutter')=='1'
     d=route.request.post_data_json;posts.append((path,d))
     if path=='config':c.update(d);s['mode']=c['mode'];s['armed']=False
     elif path=='ready/config':rc.update(d);rc['configured']=True;s['readyButton']['configured']=True;s['readyButton']['enabled']=False
     elif path=='ready/action':s['readyButton']['enabled']=d['action']=='enable'
     elif path=='action':
      if d['action']=='start':s['armed']=True
      if d['action']=='stop':s['armed']=False
      if d['action']=='test':s['sent']+=1
     route.fulfill(json={'message':'OK'});return
    data={'config':c,'status':s,'network':network,'ready/config':rc,'logs':{'events':[{'ms':1000,'text':'System ready.'},{'ms':2000,'text':'<img src=x onerror=alert(1)>'}]}}
    route.fulfill(json=data[path])
   page.route('**/api/**',api)
   page.goto(base+'/web/dashboard.html#input')
   assert page.locator('html').get_attribute('lang')=='en'
   assert page.locator('.eyebrow').count()==0
   page.locator('#input').wait_for(state='visible')
   navigate(page,'overview');page.go_back();page.locator('#input').wait_for(state='visible')
   navigate(page,'overview')
   page.get_by_text('Bluetooth ready',exact=True).wait_for()
   page.locator('[data-action=test]').click();page.wait_for_function("document.querySelector('#sent').textContent==='1'")
   navigate(page,'capture')
   page.locator('[data-preset=fifth]').click();page.locator('#capture [data-save]').click()
   page.wait_for_function("document.querySelector('#capture .saveState').textContent==='Saved'")
   assert c['every']==5
   navigate(page,'slicer');assert 'every 5 pulses' in page.locator('#gcodeSummary').inner_text()
   code=page.locator('#layerCode').inner_text();assert 'M264 P6 B1' in code and 'M264 P6 B0' in code and 'M264 P7' not in code and 'layer_num' not in code
   assert 'M262 P6 B0' in page.locator('#startCode').inner_text()
   page.screenshot(path=str(root/f'docs/configurator-{width}.png'),full_page=True)
   page.locator('#includePhoto').uncheck();assert 'M264 P6' not in page.locator('#layerCode').inner_text()
   navigate(page,'ready');page.locator('#saveReady').click()
   page.wait_for_function("document.querySelector('#readyState').textContent.startsWith('Disabled')")
   page.once('dialog',lambda d:d.accept());page.locator('#enableReady').click()
   page.wait_for_function("document.querySelector('#readyState').textContent.startsWith('Enabled')")
   assert any(path=='ready/action' and d['action']=='enable' and d['confirm']=='BESTAETIGEN' for path,d in posts)
   with page.expect_download() as download_info:page.locator('#downloadReadyMacro').click()
   download=download_info.value;assert download.suggested_filename=='btn_4.gcode'
   assert Path(download.path()).read_text()=='M1200\n'
   page.screenshot(path=str(root/f'docs/ready-{width}.png'),full_page=True)
   page.locator('#disableReady').click();page.wait_for_function("document.querySelector('#readyState').textContent.startsWith('Disabled')")
   navigate(page,'diagnostics');page.get_by_text('System ready.',exact=True).wait_for();assert page.locator('#logs img').count()==0
   page.locator('#theme').click();assert page.locator('html').get_attribute('data-theme')=='dark';page.locator('#theme').click()
   for name in ['overview','capture','input','slicer','ready','network','diagnostics','system']:
    navigate(page,name)
    assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),(width,name)
   navigate(page,'overview');page.screenshot(path=str(root/f'docs/dashboard-{width}.png'),full_page=True)
   assert not errors,errors
   page.close()
  # Installer tested with a mock element; local smoke testing also loads the real bundled module.
  page=browser.new_page(viewport={'width':390,'height':844});errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
  page.add_init_script("Object.defineProperty(navigator,'serial',{value:{requestPort:async()=>{throw new Error('No hardware')}}});")
  page.route('**/vendor/esp-web-tools-10.1.0/install-button.js',lambda r:r.fulfill(body="customElements.define('esp-web-install-button',class extends HTMLElement{});",content_type='application/javascript',headers={'access-control-allow-origin':'*'}))
  page.goto(base+'/site/index.html');page.get_by_text('Ready. Click Install to choose your device.',exact=True).wait_for()
  assert not page.locator('#installButton').is_disabled()
  assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
  page.screenshot(path=str(root/'docs/installer-mobile.png'),full_page=True);assert not errors,errors;page.close()
  page=browser.new_page();page.add_init_script("delete Navigator.prototype.serial;delete navigator.serial;")
  page.goto(base+'/site/index.html');page.get_by_text('Web Serial is unavailable. Use desktop Chrome or Edge.',exact=True).wait_for();assert page.locator('#installButton').is_disabled();page.close()
  browser.close()
 print('PASS: desktop/mobile dashboard, presets, config save, generated G-code, log escaping, mobile navigation, deep links, browser history, English copy, no eyebrows, all tab layouts, installer ready/unsupported states. Device API and installer element simulated.')
finally:server.shutdown()
