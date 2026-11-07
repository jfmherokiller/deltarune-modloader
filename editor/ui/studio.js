'use strict';
const $ = selector => document.querySelector(selector);
const api = window.editorAPI;
function log(message, level = '') {
  const line = document.createElement('div');
  line.textContent = message;
  line.className = level;
  $('#log').append(line);
  while ($('#log').children.length > 200) $('#log').firstChild.remove();
  $('#log').scrollTop = $('#log').scrollHeight;
  $('#status-message').textContent = message;
}
function report(error) { log(error.message || String(error), 'error'); }
$('#clear-output').onclick = () => $('#log').replaceChildren();
let project = null, active = null, opening = 0;
const documents = new Map();
const editor = ace.edit('code-editor');
editor.setOptions({fontSize:13, showPrintMargin:false, tabSize:4, useSoftTabs:true,
  enableBasicAutocompletion:true, enableLiveAutocompletion:true});
editor.session.setUseWorker(false);
const runtimeExplorer=window.createRuntimeInspector(api);
function showWorkspace(runtime) {
  $('#file-workspace').hidden=runtime; $('#runtime-explorer').hidden=!runtime;
  $('#workspace-mode').textContent=runtime ? 'Runtime · read-only' : 'Loose files';
  for(const [id,selected] of [['show-runtime',runtime],['show-files',!runtime]]) {
    $(`#${id}`).classList.toggle('active',selected);
    $(`#${id}`).setAttribute('aria-pressed',String(selected));
  }
  runtimeExplorer.setVisible(runtime);
  if(!runtime) editor.resize();
}
$('#show-files').onclick=()=>showWorkspace(false);
$('#show-runtime').onclick=()=>showWorkspace(true);
function element(tag, text, className) {
  const node = document.createElement(tag);
  if(text !== undefined) node.textContent = text;
  if(className) node.className = className;
  return node;
}
function shortName(file) { return file.object ? `${file.object} · ${file.event}` : file.name.replace(/^gml_GlobalScript_/,''); }
function dirty(doc) { return !!doc.session && doc.session.getValue() !== doc.saved; }
function anyDirty() { return [...documents.values()].some(dirty); }
function syncState() {
  $('#save').disabled = !active?.session || !dirty(active);
  $('#save-state').textContent = anyDirty() ? 'Unsaved changes' : 'No changes';
  document.title = `${anyDirty() ? '● ' : ''}${project?.name || 'Runner Studio'} — Runner Studio`;
  renderTabs();
}
function group(label,parent,open=true) {
  const node=element('details'); node.open=open;
  node.append(element('summary',label)); parent.append(node); return node;
}
function resourceButton(file,parent) {
  const button=element('button',undefined,`resource${active?.path === file.path ? ' selected' : ''}`);
  button.dataset.path=file.path; button.title=file.path;
  const glyph={object:'◇',script:'{}',patch:'±',sprite:'▧',origin:'⌖',sound:'♪'}[file.kind] || '·';
  button.append(element('span',glyph,'glyph'),document.createTextNode(file.object ? file.event : shortName(file)));
  if($('#scope').value==='*' && file.chapter!=='launcher') {
    const label=file.chapter==='all_chapters' ? 'shared' : file.chapter.replace(/^chapter(\d+)_windows$/,'ch$1');
    button.append(element('small',label,'scope-badge'));
  }
  button.onclick=()=>openFile(file.path).catch(report); parent.append(button);
}
function renderTree() {
  const tree=$('#tree'); tree.replaceChildren();
  const scope=$('#scope').value, query=$('#resource-filter').value.toLowerCase();
  const files=(project?.files || []).filter(file=>(scope==='*' || file.chapter===scope) && file.path.toLowerCase().includes(query));
  $('#resource-count').textContent=String(files.length);
  const objects=files.filter(file=>file.kind==='object' && file.object);
  if(objects.length) {
    const names=[...new Set(objects.map(file=>file.object))].sort();
    const node=group(`Objects (${names.length})`,tree);
    for(const name of names) {
      const objectNode=group(name,node,names.length<8 || active?.file.object===name);
      for(const file of objects.filter(file=>file.object===name)) resourceButton(file,objectNode);
    }
  }
  for(const [label,kinds] of [['Scripts',['script']],['Sprites',['sprite','origin']],['Sounds',['sound']],['Patches',['patch']]]) {
    const entries=files.filter(file=>kinds.includes(file.kind));
    if(!entries.length) continue;
    const node=group(`${label} (${entries.length})`,tree);
    for(const file of entries) resourceButton(file,node);
  }
  if(!files.length) tree.append(element('p','No supported loose resources in this scope.','muted inset'));
}
function renderTabs() {
  $('#tabs').replaceChildren();
  for(const doc of documents.values()) {
    const tab=element('div',undefined,`tab${active===doc ? ' active' : ''}`);
    const button=element('button',`${dirty(doc) ? '● ' : ''}${shortName(doc.file)}`);
    button.setAttribute('role','tab'); button.setAttribute('aria-selected',String(active===doc));
    button.title=doc.path; button.onclick=()=>activate(doc);
    const close=element('button','×','close'); close.setAttribute('aria-label',`Close ${doc.file.name}`);
    close.onclick=()=>{
      if(dirty(doc) && !confirm(`Discard unsaved changes to ${doc.file.name}?`)) return;
      documents.delete(doc.path);
      if(active===doc) activate([...documents.values()].at(-1) || null);
      syncState();
    };
    tab.append(button,close); $('#tabs').append(tab);
  }
}
function properties(parent,items) {
  const list=element('dl',undefined,'properties');
  for(const [name,value] of items) {
    const row=element('div'); row.append(element('dt',name),element('dd',String(value ?? '—'))); list.append(row);
  }
  parent.append(list);
}
function inspect(doc) {
  const panel=$('#resource-inspector'); panel.replaceChildren();
  if(!doc) { panel.append(element('p','Select a resource to inspect its properties.','muted inset')); return; }
  const heading=element('div',undefined,'resource-title');
  heading.append(element('span',doc.file.kind.toUpperCase(),'kind-icon'),element('b',doc.file.object || doc.file.name)); panel.append(heading);
  properties(panel,[['Scope',doc.file.chapter==='launcher' ? 'Root / game' : doc.file.chapter],['Resource',doc.file.kind],['File',doc.path],['Apply','Requires verified reload backend']]);
  if(doc.session) {
    panel.append(element('div','Outline','section-label'));
    const outline=element('div',undefined,'outline'); panel.append(outline);
    let found=0;
    doc.session.getValue().split('\n').forEach((line,index)=>{
      const match=line.match(/^\s*(?:function\s+([\w]+)|#region\s+(.+)|#macro\s+([\w]+))/);
      if(!match || found++>=150) return;
      const button=element('button',match[1] || match[2] || match[3]);
      button.onclick=()=>{ editor.gotoLine(index+1); editor.focus(); }; outline.append(button);
    });
    if(!found) outline.append(element('p','No function or region declarations.','muted inset'));
  }
}
function activate(doc) {
  showWorkspace(false);
  active=doc;
  $('#welcome').hidden=!!doc; $('#code-editor').hidden=!doc?.session;
  $('#asset-preview').hidden=!doc || !!doc.session;
  $('#event-strip').replaceChildren(); $('#event-strip').hidden=!doc?.file.object;
  $('#document-path').textContent=doc?.path || 'No resource selected';
  if(doc?.file.object) for(const file of project.files.filter(file=>file.object===doc.file.object && file.chapter===doc.file.chapter)) {
    const button=element('button',file.event,file.path===doc.path ? 'active' : '');
    button.onclick=()=>openFile(file.path).catch(report); $('#event-strip').append(button);
  }
  if(doc?.session) { editor.setSession(doc.session); editor.resize(); editor.focus(); }
  else if(doc) preview(doc);
  inspect(doc); syncState(); renderTree();
}
async function openFile(path,line) {
  const ticket=++opening;
  let doc=documents.get(path);
  if(!doc) {
    const file=project.files.find(file=>file.path===path);
    if(!file) throw new Error('Resource is no longer in the workspace.');
    const result=await api.read(path); doc={...result,path,file,saved:result.text};
    if(typeof result.text==='string') {
      doc.session=ace.createEditSession(result.text,/\.(diff|patch)$/.test(path) ? 'ace/mode/diff' : 'ace/mode/gml');
      doc.session.setUseWorker(false); doc.session.setTabSize(4);
      doc.session.on('change',()=>{ syncState(); if(active===doc) inspect(doc); });
    }
    documents.set(path,doc);
  }
  if(ticket!==opening) return;
  activate(doc); if(line && doc.session) editor.gotoLine(line);
}
function preview(doc) {
  const host=$('#asset-preview'); host.replaceChildren(); host.append(element('h2',doc.file.name));
  if(doc.file.kind==='sprite') {
    const stage=element('div',undefined,'preview-stage'), image=element('img');
    image.alt=doc.file.name;
    const controls=element('div',undefined,'preview-controls'), label=element('label','Zoom '), zoom=element('input'), info=element('span','Loading image…');
    zoom.type='range'; zoom.min='1'; zoom.max='12'; zoom.value='3'; zoom.setAttribute('aria-label','Sprite zoom');
    const resize=()=>{ image.width=image.naturalWidth*Number(zoom.value); image.height=image.naturalHeight*Number(zoom.value); };
    image.onload=()=>{ info.textContent=`${image.naturalWidth} × ${image.naturalHeight} px`; resize(); };
    image.onerror=()=>report(new Error('This PNG could not be decoded.'));
    image.src=doc.dataUrl;
    zoom.oninput=resize; label.append(zoom); controls.append(label,info); stage.append(image); host.append(controls,stage);
    host.append(element('p','Pixel preview. Edit the source PNG in your image editor; close and reopen this resource to refresh.','muted'));
  } else if(doc.file.kind==='sound') {
    const audio=element('audio'); audio.controls=true; audio.src=doc.dataUrl; audio.preload='metadata'; host.append(audio);
    host.append(element('p','Local preview only. Playback here does not trigger audio in the game.','muted'));
  }
}
async function save() {
  const doc=active;
  if(!doc?.session || !dirty(doc)) return;
  const text=doc.session.getValue(); $('#save').disabled=true;
  try {
    const result=await api.save(doc.path,text,doc.revision);
    doc.revision=result.revision; doc.saved=text; syncState();
    if(!anyDirty()) $('#save-state').textContent='Saved · requires reload';
    log(`Saved ${doc.path}. Not applied to the running game.`,'success');
  } catch(error) { syncState(); throw error; }
}
async function loadProject(value) {
  if(!value) return;
  project=value; documents.clear(); active=null;
  $('#project-name').textContent=value.name; $('#project-name').title=value.root;
  const scope=$('#scope'); scope.replaceChildren();
  const all=element('option','All resources'); all.value='*'; scope.append(all);
  for(const name of [...new Set(value.files.map(file=>file.chapter))].sort()) {
    const option=element('option',name==='launcher' ? 'Root / game' : name); option.value=name; scope.append(option);
  }
  for(const id of ['scope','quick-open','find']) $(`#${id}`).disabled=false;
  $('#resource-filter').value=''; activate(null);
  log(`Opened ${value.name}: ${value.files.length} loose resources.`,'success');
}
$('#open-mod').onclick=async()=>{
  if(anyDirty() && !confirm('Discard unsaved changes and open another mod?')) return;
  try { await loadProject(await api.choose()); } catch(error) { report(error); }
};
$('#save').onclick=()=>save().catch(report);
$('#scope').onchange=renderTree; $('#resource-filter').oninput=renderTree;
function showOutput(results) {
  $('#log').hidden=results; $('#results').hidden=!results;
  $('#show-output').classList.toggle('active',!results); $('#show-results').classList.toggle('active',results);
}
$('#show-output').onclick=()=>showOutput(false); $('#show-results').onclick=()=>showOutput(true);
$('#find').onclick=()=>{ $('#search-dialog').showModal(); $('#search-input').focus(); };
$('#cancel-search').onclick=()=>$('#search-dialog').close();
$('#search-form').onsubmit=async event=>{
  event.preventDefault();
  try {
    const matches=await api.search($('#search-input').value); $('#results').replaceChildren();
    for(const match of matches) {
      const button=element('button',`${match.path}:${match.line}  ${match.text}`);
      button.onclick=()=>openFile(match.path,match.line).catch(report); $('#results').append(button);
    }
    if(!matches.length) $('#results').append(element('p','No matches in saved files.'));
    $('#match-count').textContent=String(matches.length); showOutput(true); $('#search-dialog').close();
  } catch(error) { report(error); }
};
function updatePicker() {
  const query=$('#picker-input').value.toLowerCase(); $('#picker-results').replaceChildren();
  for(const file of (project?.files || []).filter(file=>file.path.toLowerCase().includes(query)).slice(0,100)) {
    const button=element('button',shortName(file)); button.append(element('small',file.path));
    button.onclick=()=>{ $('#picker').close(); openFile(file.path).catch(report); }; $('#picker-results').append(button);
  }
}
$('#quick-open').onclick=()=>{ $('#picker-input').value=''; updatePicker(); $('#picker').showModal(); $('#picker-input').focus(); };
$('#picker-input').oninput=updatePicker;
$('#picker-input').onkeydown=event=>{ if(event.key==='Enter') { event.preventDefault(); $('#picker-results button')?.click(); } };
editor.commands.addCommand({name:'saveLooseFile',bindKey:{win:'Ctrl-S',mac:'Command-S'},exec:()=>save().catch(report)});
editor.completers=[{getCompletions(_editor,session,_pos,prefix,callback) {
  const names=new Set(['if','else','var','function','return','switch','case','break','with','global','self','other','true','false','undefined']);
  for(const file of project?.files || []) names.add(file.object || file.name.replace(/^gml_GlobalScript_/,'').replace(/\.(gml|png|ogg|wav)$/,''));
  for(const name of session.getValue().match(/\b[a-zA-Z_]\w*\b/g) || []) names.add(name);
  callback(null,[...names].filter(name=>name.startsWith(prefix)).slice(0,150).map(name=>({caption:name,value:name,meta:'workspace',score:100})));
}}];
editor.selection.on('changeCursor',()=>{ const cursor=editor.getCursorPosition(); $('#cursor').textContent=`Ln ${cursor.row+1}, Col ${cursor.column+1}`; });
window.addEventListener('keydown',event=>{
  if(!(event.ctrlKey || event.metaKey)) return;
  if(event.key.toLowerCase()==='p' && project) { event.preventDefault(); $('#quick-open').click(); }
  if(event.key.toLowerCase()==='f' && event.shiftKey && project) { event.preventDefault(); $('#find').click(); }
  if(event.key.toLowerCase()==='s') { event.preventDefault(); save().catch(report); }
});
window.addEventListener('beforeunload',event=>{ if(anyDirty()) { event.preventDefault(); event.returnValue=false; } });
new ResizeObserver(()=>editor.resize()).observe($('#document-area'));
api.initial().then(loadProject).catch(report);
let connectedPid=null, polling=false, runtimeSession=0, connectionAttempt=0;
function disconnect(reason='Disconnected') {
  ++runtimeSession; ++connectionAttempt;
  connectedPid=null;
  runtimeExplorer.disconnect(reason);
  $('#connect').textContent='Connect';
  $('#runtime-status').textContent='Disconnected';
  $('.runtime').classList.remove('connected');
  $('#runtime-details').replaceChildren(element('p',reason,'muted'));
}
function renderSnapshot(sample) {
  runtimeExplorer.snapshot(sample);
  const details=$('#runtime-details'); details.replaceChildren();
  const fields=[['Process',sample.pid],['Room',sample.room],['Runner',({vm:'VM (bytecode)',yyc:'YYC (compiled)'})[sample.runnerType] || 'Unknown'],['Events',sample.events],['Sample age',`${sample.ageMs ?? '?'} ms`],['Mode','Read-only inspection']];
  properties(details,fields);
  details.append(element('p','Live code and asset replacement are not implemented in this bridge.','muted'));
  $('#runtime-status').textContent='Connected';
  $('.runtime').classList.add('connected');
}
async function refreshRuntimes() {
  const list=$('#runtime-list'); list.replaceChildren(element('p','Looking for local bridges…','muted'));
  try {
    const runtimes=await api.discover(); list.replaceChildren();
    for(const runtime of runtimes) {
      const button=element('button',`Inspect process ${runtime.pid}`);
      button.onclick=async()=>{
        const attempt=++connectionAttempt;
        try {
          const sample=await api.snapshot(runtime.pid);
          if(attempt!==connectionAttempt || !$('#runtime-dialog').open) return;
          ++runtimeSession;
          runtimeExplorer.disconnect();
          connectedPid=runtime.pid; renderSnapshot(sample);
          $('#connect').textContent='Disconnect'; $('#runtime-dialog').close();
          log(`Connected read-only to process ${runtime.pid}.`,'success');
        } catch(error) { if(attempt===connectionAttempt) report(error); }
      };
      list.append(button);
    }
    if(!runtimes.length) list.append(element('p','No RunnerEditor bridges found. The game must have the compatible bridge plugin loaded.','muted'));
  } catch(error) { list.replaceChildren(element('p',error.message,'muted')); report(error); }
}
$('#connect').onclick=()=>{
  if(connectedPid!==null) { disconnect(); log('Runtime disconnected.'); return; }
  $('#runtime-dialog').showModal(); refreshRuntimes();
};
$('#runtime-dialog').addEventListener('close',()=>{ ++connectionAttempt; });
$('#refresh-runtimes').onclick=refreshRuntimes;
setInterval(async()=>{
  if(connectedPid===null || polling) return;
  polling=true; const pid=connectedPid, session=runtimeSession;
  try { const sample=await api.snapshot(pid); if(session===runtimeSession && pid===connectedPid) renderSnapshot(sample); }
  catch(error) { if(session===runtimeSession && pid===connectedPid) { disconnect('The runtime is unavailable. Reconnect after launching or switching games.'); report(error); } }
  finally { polling=false; }
},1000);
