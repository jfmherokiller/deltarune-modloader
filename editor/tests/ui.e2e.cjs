const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');
const net = require('node:net');

// Both endpoints are synthetic fixtures; no game is installed or launched.
async function syntheticInspector() {
  const pid = process.pid;
  const state = { room:'SYNTHETIC_fixture_room', revision:1, error:null, delay:0, capability:true, commands:[], sockets:new Set(), held:null, globalCount:66, pageSize:{globals:64,instances:64,instance:64} };
  const field = (name,type,value,source='dynamic') => ({name,type,value,source,readOnly:true});
  const globals = () => [
    field('fixture_revision','int32',String(state.revision)),
    field('exact_integer','int64','9223372036854775807'),
    field('<img src=x onerror=alert(1)>','string','<script>window.fixtureInjected = true</script>'),
    ...Array.from({length:state.globalCount-3},(_,i)=>field(`global_${String(i).padStart(2,'0')}`,'real',String(i)))
  ];
  const telemetry = net.createServer(socket => socket.end(JSON.stringify({
    protocol:1,pid,room:state.room,events:12,ageMs:0,sampledAt:Date.now(),runnerType:'unknown',
    capabilities:{inspect:true,inspectVariables:state.capability,liveCode:false,liveAssets:false}
  })+'\n'));
  const inspector = net.createServer(socket => {
    state.sockets.add(socket); socket.on('close',()=>state.sockets.delete(socket)); socket.on('error',()=>{});
    let request='';
    socket.on('data',chunk=>{
      request+=chunk.toString('ascii');
      if(!request.includes('\n')) return;
      // Exact ASCII grammar, no JSON requests or telemetry on this endpoint.
      assert.match(request,/^(?:globals [0-9]+|instances [0-9]+|instance [0-9]+ [0-9]+)\n$/);
      state.commands.push(request);
      const parts=request.trim().split(' '), kind=parts[0];
      const target=kind==='instance' ? Number(parts[1]) : null, offset=Number(parts.at(-1));
      const all=kind==='globals' ? globals() : kind==='instances' ? [
        {id:100001,object:'obj_fixture_player'}, {id:100002,object:'obj_fixture_enemy'}
      ] : [field('id','int32',String(target),'builtin'),field('x','real',target===100001 ? '12.5' : '42','builtin'),
        field('visible','bool','true','builtin'),field('label','string',`instance-${target}`),field('large','int64','9223372036854775807')];
      const size=state.pageSize[kind];
      const result={protocol:2,pid,kind,target,offset,items:state.error!==null ? [] : all.slice(offset,offset+size),
        hasMore:state.error===null && all.length>offset+size,room:state.error!==null ? null : state.room,ageMs:0,sampledAt:Date.now(),error:state.error};
      const reply=()=>{ if(!socket.destroyed) { result.sampledAt=Date.now(); socket.end(JSON.stringify(result)+'\n'); } };
      if(state.held?.command===request) { const hold=state.held; state.held=null; hold.release=reply; hold.resolve(); }
      else if(state.delay) setTimeout(reply,state.delay); else reply();
    });
  });
  const listen=(server,name)=>new Promise((resolve,reject)=>{server.once('error',reject);server.listen(`\\\\.\\pipe\\${name}-${pid}`,resolve);});
  const stop=server=>new Promise(resolve=>server.listening ? server.close(resolve) : resolve());
  await listen(telemetry,'RunnerEditor');
  try { await listen(inspector,'RunnerInspector'); } catch(error) { await stop(telemetry); throw error; }
  return {pid,state,stopTelemetry:()=>stop(telemetry),holdNext(command) {
    const hold={command,release:null}; hold.received=new Promise(resolve=>{hold.resolve=resolve;}); state.held=hold; return hold;
  },async close() {
    for(const socket of state.sockets) socket.destroy();
    await Promise.all([stop(telemetry),stop(inspector)]);
  }};
}
async function launchFixture(args=[]) {
  const profile=await fs.mkdtemp(path.join(process.env.TMPDIR || os.tmpdir(),'runner-inspector-ui-'));
  const app=await electron.launch({args:[path.resolve(__dirname,'..'),`--user-data-dir=${profile}`,...args]});
  const page=await app.firstWindow(); page.setDefaultTimeout(6000);
  return {app,page,async close(){await app.close();await fs.rm(profile,{recursive:true,force:true});}};
}
async function connectFixture(page,pid) {
  await page.getByRole('button',{name:'Connect',exact:true}).click();
  await page.getByRole('button',{name:`Inspect process ${pid}`,exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#runtime-status').textContent==='Connected');
}

test('Runtime Explorer reads paged typed globals without a mod and never interprets values as HTML', {timeout:30000}, async()=>{
  const fixture=await syntheticInspector(); let ui;
  try {
    ui=await launchFixture(); const {page}=ui;
    await page.getByRole('button',{name:'Runtime Explorer',exact:true}).click();
    assert.match(await page.locator('#runtime-explorer').innerText(),/Connect to a runtime/);
    await connectFixture(page,fixture.pid);
    await page.locator('#runtime-fields tbody tr').first().waitFor();
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),64);
    assert.match(await page.locator('#runtime-fields').innerText(),/9223372036854775807/);
    assert.match(await page.locator('#runtime-fields').innerText(),/<script>window.fixtureInjected = true<\/script>/);
    assert.equal(await page.locator('#runtime-fields img, #runtime-fields script').count(),0);
    assert.equal(await page.evaluate(()=>window.fixtureInjected),undefined);
    assert.match(await page.locator('#runtime-limits').innerText(),/64/);
    await page.getByRole('button',{name:'Load more fields',exact:true}).click();
    await page.waitForFunction(()=>document.querySelectorAll('#runtime-fields tbody tr').length===66);
    assert.ok(fixture.state.commands.includes('globals 64\n'));
    await page.getByRole('searchbox',{name:'Filter loaded fields',exact:true}).fill('exact_integer');
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),1);
    await page.getByRole('searchbox',{name:'Filter loaded fields',exact:true}).fill('');
    fixture.state.revision=2;
    await page.getByRole('button',{name:'Refresh fields',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('#runtime-fields tbody tr td:last-child')?.textContent==='2');
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),64);
    assert.equal(await page.getByRole('button',{name:'Apply live',exact:true}).isDisabled(),true);
    assert.match(await page.locator('#runtime-sample-age').innerText(),/ago|ms|seconds/);
  } finally {if(ui) await ui.close();await fixture.close();}
});

test('instance groups page independently and late target responses never repaint another selection', {timeout:30000}, async()=>{
  const fixture=await syntheticInspector(); fixture.state.pageSize.instances=1; fixture.state.pageSize.instance=3; let ui;
  try {
    ui=await launchFixture(); const {page}=ui;
    await connectFixture(page,fixture.pid);
    await page.getByRole('button',{name:'Runtime Explorer',exact:true}).click();
    await page.getByRole('button',{name:'Instances',exact:true}).click();
    await page.getByRole('button',{name:'Inspect instance 100001',exact:true}).waitFor();
    assert.match(await page.locator('#runtime-object-groups').innerText(),/obj_fixture_player/);
    await page.getByRole('button',{name:'Load more instances',exact:true}).click();
    await page.getByRole('button',{name:'Inspect instance 100002',exact:true}).waitFor();
    assert.ok(fixture.state.commands.includes('instances 1\n'));
    await page.getByRole('button',{name:'Inspect instance 100001',exact:true}).click();
    await page.waitForFunction(()=>document.querySelectorAll('#runtime-fields tbody tr').length===3);
    assert.match(await page.locator('#runtime-fields').innerText(),/builtin/);
    assert.match(await page.locator('#runtime-fields').innerText(),/12.5/);
    const delayed=fixture.holdNext('instance 100001 3\n');
    await page.getByRole('button',{name:'Load more fields',exact:true}).click(); await delayed.received;
    await page.getByRole('button',{name:'Inspect instance 100002',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('#runtime-fields tbody tr td:last-child')?.textContent==='100002');
    delayed.release(); await page.waitForTimeout(150); // Allow the older real IPC response to settle.
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),3);
    assert.doesNotMatch(await page.locator('#runtime-fields').innerText(),/instance-100001|12.5/);
    await page.getByRole('button',{name:'Load more fields',exact:true}).click();
    await page.waitForFunction(()=>document.querySelectorAll('#runtime-fields tbody tr').length===5);
    assert.match(await page.locator('#runtime-fields').innerText(),/instance-100002/);
    assert.match(await page.locator('#runtime-fields').innerText(),/9223372036854775807/);
    assert.ok(fixture.state.commands.includes('instance 100002 3\n'));
    await page.getByRole('searchbox',{name:'Filter loaded instances',exact:true}).fill('enemy');
    assert.equal(await page.locator('#runtime-object-groups button').count(),1);
    const commands=fixture.state.commands.length;
    await page.waitForTimeout(1250); // Cross the telemetry poll interval; inspection must remain manual.
    assert.equal(fixture.state.commands.length,commands);
    await page.getByRole('button',{name:'Refresh instances',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('#runtime-instance-status').textContent.includes('1 loaded'));
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),0);
    assert.equal(await page.getByRole('button',{name:'Refresh fields',exact:true}).isDisabled(),true);
  } finally {if(ui) await ui.close();await fixture.close();}
});

test('failed pages and room changes clear values, and same-PID reconnect invalidates pending requests', {timeout:40000}, async()=>{
  const fixture=await syntheticInspector(); let ui;
  try {
    ui=await launchFixture(); const {page}=ui;
    await page.getByRole('button',{name:'Runtime Explorer',exact:true}).click();
    await connectFixture(page,fixture.pid);
    await page.locator('#runtime-fields tbody tr').first().waitFor();
    fixture.state.error='SYNTHETIC inspection failed';
    await page.getByRole('button',{name:'Load more fields',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('#runtime-field-status').textContent.includes('Request failed'));
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),0);
    assert.equal(await page.locator('#runtime-more-fields').isVisible(),false);
    assert.equal(await page.locator('#runtime-sample-age').innerText(),'No variable sample');
    fixture.state.error=''; // The protocol distinguishes even an empty error string from null.
    await page.getByRole('button',{name:'Refresh fields',exact:true}).click();
    await page.waitForFunction(()=>!document.querySelector('#runtime-refresh-fields').disabled);
    assert.match(await page.locator('#runtime-field-status').innerText(),/Request failed/);
    fixture.state.error=null;
    await page.getByRole('button',{name:'Refresh fields',exact:true}).click();
    await page.locator('#runtime-fields tbody tr').first().waitFor();
    fixture.state.room='SYNTHETIC_next_room';
    await page.waitForFunction(()=>document.querySelector('#runtime-context').textContent.includes('SYNTHETIC_next_room'));
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),0);
    assert.match(await page.locator('#runtime-notice').innerText(),/changed/);
    await page.getByRole('button',{name:'Refresh fields',exact:true}).click();
    await page.locator('#runtime-fields tbody tr').first().waitFor();
    const delayed=fixture.holdNext('globals 64\n');
    await page.getByRole('button',{name:'Load more fields',exact:true}).click(); await delayed.received;
    await page.getByRole('button',{name:'Disconnect',exact:true}).click();
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),0);
    fixture.state.revision=7;
    await connectFixture(page,fixture.pid);
    await page.waitForFunction(()=>document.querySelector('#runtime-fields tbody tr td:last-child')?.textContent==='7');
    delayed.release(); await page.waitForTimeout(150);
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),64);
    assert.equal(await page.locator('#runtime-fields tbody tr td:last-child').first().innerText(),'7');
    await fixture.stopTelemetry();
    await page.waitForFunction(()=>document.querySelector('#runtime-status').textContent==='Disconnected');
    assert.equal(await page.locator('#runtime-fields tbody tr').count(),0);
    assert.equal(await page.locator('#runtime-object-groups button').count(),0);
    assert.equal(await page.locator('#runtime-sample-age').innerText(),'No variable sample');
  } finally {if(ui) await ui.close();await fixture.close();}
});

test('loaded-row cap also bounds a short final page and reports omitted rows honestly', {timeout:60000}, async()=>{
  const fixture=await syntheticInspector(); fixture.state.globalCount=4100; fixture.state.pageSize.globals=63; let ui;
  try {
    ui=await launchFixture(); const {page}=ui;
    await connectFixture(page,fixture.pid);
    await page.getByRole('button',{name:'Runtime Explorer',exact:true}).click();
    await page.locator('#runtime-fields tbody tr').first().waitFor();
    await page.getByRole('searchbox',{name:'Filter loaded fields',exact:true}).fill('exact_integer');
    for(let offset=63;offset<4096;offset+=63) {
      await page.getByRole('button',{name:'Load more fields',exact:true}).click();
      await page.waitForFunction(()=>!document.querySelector('#runtime-refresh-fields').disabled);
    }
    assert.match(await page.locator('#runtime-field-status').innerText(),/1 shown \/ 4096 loaded/);
    assert.match(await page.locator('#runtime-field-status').innerText(),/limit reached/);
    assert.equal(await page.getByRole('button',{name:'Load more fields',exact:true}).isDisabled(),true);
    assert.match(await page.locator('#runtime-limits').innerText(),/512-byte/);
    assert.ok(fixture.state.commands.includes('globals 4095\n'));
    await page.getByRole('button',{name:'Refresh fields',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('#runtime-field-status').textContent.includes('63 loaded'));
    assert.equal(await page.getByRole('button',{name:'Load more fields',exact:true}).isDisabled(),false);
  } finally {if(ui) await ui.close();await fixture.close();}
});

test('older telemetry bridges retain the update-needed message across explorer tabs', {timeout:30000}, async()=>{
  const fixture=await syntheticInspector(); fixture.state.capability=false; let ui;
  try {
    ui=await launchFixture(); const {page}=ui;
    await connectFixture(page,fixture.pid);
    await page.getByRole('button',{name:'Runtime Explorer',exact:true}).click();
    assert.match(await page.locator('#runtime-notice').innerText(),/Connector update needed/);
    await page.getByRole('button',{name:'Instances',exact:true}).click();
    assert.match(await page.locator('#runtime-notice').innerText(),/Connector update needed/);
    assert.deepEqual(fixture.state.commands,[]);
    assert.equal(await page.locator('#runtime-content').isVisible(),false);
  } finally {if(ui) await ui.close();await fixture.close();}
});

test('runtime panel connects through a SYNTHETIC local bridge and clears stale state', { timeout: 30000 }, async () => {
  const pid = process.pid;
  const server = net.createServer(socket => socket.end(JSON.stringify({
    protocol:1,pid,room:'SYNTHETIC_fixture_room',events:12,ageMs:0,sampledAt:Date.now(),
    runnerType:'unknown',capabilities:{inspect:true,liveCode:false,liveAssets:false}
  }) + '\n'));
  await new Promise((resolve,reject) => { server.once('error',reject); server.listen(`\\\\.\\pipe\\RunnerEditor-${pid}`,resolve); });
  let app;
  try {
    app = await electron.launch({args:[path.resolve(__dirname,'..')]});
    const page = await app.firstWindow(); page.setDefaultTimeout(6000);
    await page.getByRole('button',{name:'Connect',exact:true}).click();
    await page.getByRole('button',{name:`Inspect process ${pid}`,exact:true}).click();
    await page.waitForFunction(() => document.querySelector('#runtime-status').textContent === 'Connected');
    assert.match(await page.locator('#runtime-details').innerText(),/SYNTHETIC_fixture_room/);
    assert.equal(await page.getByRole('button',{name:'Apply live',exact:true}).isDisabled(),true);
    await new Promise(resolve => server.close(resolve));
    await page.waitForFunction(() => document.querySelector('#runtime-status').textContent === 'Disconnected');
    assert.doesNotMatch(await page.locator('#runtime-details').innerText(),/SYNTHETIC_fixture_room/);
  } finally {
    if(app) await app.close();
    if(server.listening) await new Promise(resolve => server.close(resolve));
  }
});

test('desktop starts with an honest disconnected state and local-only renderer', async () => {
  const profile = await fs.mkdtemp(path.join(process.env.TMPDIR || os.tmpdir(), 'dr-editor-ui-'));
  let app;
  try {
    app = await electron.launch({ args: [path.resolve(__dirname, '..'), `--user-data-dir=${profile}`] });
    const page = await app.firstWindow();
    await page.getByRole('button', { name: 'Open Mod', exact: true }).waitFor();
    assert.equal(await page.locator('#runtime-status').innerText(), 'Disconnected');
    assert.equal(await page.getByRole('button', { name: 'Apply live', exact: true }).isDisabled(), true);
    assert.equal(await page.evaluate(() => typeof window.require), 'undefined');
    assert.equal(await page.locator('#asset-browser').count(), 1);
    assert.equal(await page.locator('#inspector').count(), 1);
    await page.getByRole('button', { name:'Connect', exact:true }).click();
    await page.locator('#runtime-dialog').waitFor({state:'visible'});
    assert.match(await page.locator('#runtime-dialog').innerText(), /read-only/);
  } finally {
    if (app) await app.close();
    await fs.rm(profile, { recursive: true, force: true });
  }
});

test('edit a real loose event, save it, search it, and preview a sprite', { timeout: 90000 }, async () => {
  const root = await fs.mkdtemp(path.join(process.env.TMPDIR || os.tmpdir(), 'runner-workspace-'));
  const relative = 'code/gml_Object_obj_example_Create_0.gml';
  await fs.mkdir(path.join(root, 'code'));
  await fs.mkdir(path.join(root, 'sprites'));
  for(const chapter of ['chapter1_windows','chapter2_windows']) {
    await fs.mkdir(path.join(root,chapter,'code'),{recursive:true});
    await fs.writeFile(path.join(root,chapter,'code','gml_GlobalScript_skill.gml'),'return 1;\n');
  }
  await fs.writeFile(path.join(root, relative), 'var speed = 2; // sample\n');
  await fs.writeFile(path.join(root, 'sprites/spr_example_0.png'), Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+afo0AAAAASUVORK5CYII=', 'base64'));
  let app;
  try {
    app = await electron.launch({ args: [path.resolve(__dirname, '..'), `--workspace=${root}`, `--user-data-dir=${path.join(root, '.profile')}`] });
    const page = await app.firstWindow();
    page.setDefaultTimeout(6000);
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.locator('[data-path="' + relative + '"]').click();
    await page.locator('#code-editor').waitFor({state:'visible'});
    assert.match(await page.locator('[data-path="chapter2_windows/code/gml_GlobalScript_skill.gml"]').innerText(),/ch2/);
    await page.locator('#code-editor .ace_comment').first().waitFor({state:'visible'});
    assert.equal(await page.evaluate(() => ace.edit('code-editor').session.getMode().$id), 'ace/mode/gml');
    await page.keyboard.press('Control+f');
    await page.locator('.ace_search').waitFor({state:'visible'});
    await page.keyboard.press('Escape');
    assert.match(await page.locator('#code-editor .ace_keyword').first().innerText(),/var/);
    await page.evaluate(() => ace.edit('code-editor').setValue('speed = 3;\n', -1));
    await page.getByRole('button',{name:'Runtime Explorer',exact:true}).click();
    assert.equal(await page.locator('#code-editor').isVisible(),false);
    assert.match(await page.locator('#save-state').innerText(),/Unsaved changes/);
    await page.getByRole('button',{name:'File editor',exact:true}).click();
    assert.equal(await page.evaluate(()=>ace.edit('code-editor').getValue()),'speed = 3;\n');
    await page.getByRole('button',{name:'Runtime Explorer',exact:true}).click();
    await page.locator('[data-path="' + relative + '"]').click();
    assert.equal(await page.locator('#code-editor').isVisible(),true);
    assert.equal(await page.locator('#runtime-explorer').isVisible(),false);
    assert.equal(await page.evaluate(()=>ace.edit('code-editor').getValue()),'speed = 3;\n');
    await page.getByRole('button', { name:'Save', exact:true }).click();
    await page.waitForFunction(() => document.querySelector('#save-state').textContent === 'Saved · requires reload');
    assert.equal(await fs.readFile(path.join(root,relative),'utf8'), 'speed = 3;\n');
    await page.getByRole('button', {name:'Find in files',exact:true}).click();
    await page.getByRole('textbox',{name:'Search text',exact:true}).fill('speed = 3');
    await page.getByRole('button',{name:'Search',exact:true}).click();
    await page.locator('#results button').first().waitFor();
    assert.match(await page.locator('#results').innerText(),/speed = 3/);
    await page.locator('[data-path="sprites/spr_example_0.png"]').click();
    await page.locator('#asset-preview img').waitFor();
    await page.waitForFunction(() => document.querySelector('#asset-preview img').naturalWidth === 1);
    assert.deepEqual(errors,[]);
  } finally {
    if(app) await app.close();
    await fs.rm(root,{recursive:true,force:true});
  }
});
