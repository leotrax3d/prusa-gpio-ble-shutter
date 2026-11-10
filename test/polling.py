"""Browser request-count and incremental log regression tests; mocked ESP32 API."""
import asyncio,copy,json,threading,http.server,functools,os,shutil
from pathlib import Path
from urllib.parse import urlparse,parse_qs
from playwright.async_api import async_playwright
from fixtures import config,servo,network,status
root=Path(__file__).resolve().parents[1]
class Handler(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*args):pass
async def main():
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(root)))
 threading.Thread(target=server.serve_forever,daemon=True).start()
 requests=[];generation=7;events=[dict(id='1',ms=1000,text='First')];cursor=1;fail=False;payloadCounts=[]
 try:
  async with async_playwright() as p:
   browser=await p.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH') or shutil.which('chromium'),headless=True,args=['--no-sandbox'])
   page=await browser.new_page(viewport=dict(width=1440,height=900))
   await page.add_init_script("Object.defineProperty(document,'hidden',{get:()=>window.__testHidden||false});")
   async def route(r):
    u=urlparse(r.request.url);path=u.path.split('/api/')[1];q=parse_qs(u.query);requests.append((path,q))
    if path=='status' and fail:await r.fulfill(status=503,json={'error':'Offline test'});return
    if path=='logs':
     after=int(q.get('after',['0'])[0]);reset=int(q.get('generation',['0'])[0])!=generation or after>cursor
     batch=events if reset else [e for e in events if int(e['id'])>after]
     payloadCounts.append(len(batch));await r.fulfill(json=dict(generation=generation,cursor=str(cursor),reset=reset,events=batch));return
    data={'status':status,'config':config,'servo/config':servo,'network':network,'ready/config':dict(configured=False,input=32,output=25,debounce=30,pulse=1000,cooldown=2000)}
    await r.fulfill(json=data[path])
   await page.route('**/api/**',route)
   await page.goto(f'http://127.0.0.1:{server.server_port}/web/dashboard.html')
   await page.get_by_text('Bluetooth ready',exact=True).wait_for()
   before=sum(p=='status' for p,q in requests)
   await page.wait_for_timeout(10200)
   periodic=sum(p=='status' for p,q in requests)-before
   assert 2<=periodic<=4,periodic
   assert not any(p=='logs' for p,q in requests)
   await page.locator('nav [data-tab=diagnostics]').click()
   await page.get_by_text('First',exact=True).wait_for()
   await page.evaluate("window.originalLogRow=document.querySelector('#logs tr')")
   await page.evaluate('async()=>await loadLogs()')
   assert payloadCounts[-1]==0
   assert await page.evaluate("document.querySelector('#logs tr')===window.originalLogRow")
   events.append(dict(id='2',ms=2000,text='Second'));cursor=2
   await page.evaluate('async()=>await loadLogs()')
   assert payloadCounts[-1]==1 and await page.locator('#logs tr').count()==2
   assert await page.evaluate("document.querySelectorAll('#logs tr')[1]===window.originalLogRow")
   assert any(p=='logs' and q.get('after')==['1'] for p,q in requests)
   generation=8;events=[];cursor=0
   await page.evaluate('async()=>await loadLogs()');assert await page.locator('#logs tr').count()==0
   generation=9;events=[dict(id='1',ms=10,text='Restarted')];cursor=1
   await page.evaluate('async()=>await loadLogs()');assert await page.locator('#logs tr').count()==1
   await page.evaluate("window.__testHidden=true;document.dispatchEvent(new Event('visibilitychange'))")
   await page.wait_for_timeout(100);before=len(requests);await page.wait_for_timeout(3300);assert len(requests)==before
   await page.evaluate("window.__testHidden=false;document.dispatchEvent(new Event('visibilitychange'))")
   await page.wait_for_timeout(200);assert len(requests)>before
   fail=True;await page.evaluate('async()=>await poll()');before=sum(p=='status' for p,q in requests)
   await page.wait_for_timeout(3200);assert sum(p=='status' for p,q in requests)==before
   await browser.close()
   print(json.dumps({'periodic_status_requests_in_10s':periodic,'hidden_tab_requests':0,'unchanged_log_events_transferred':0,'new_log_events_transferred':1,'checks':['incremental logs','reboot/clear reset','DOM node reuse','visibility pause','offline backoff']}))
 finally:server.shutdown()
asyncio.run(main())
