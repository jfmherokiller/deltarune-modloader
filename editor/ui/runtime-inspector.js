'use strict';
// The renderer only consumes display strings. It never evaluates GML or writes runtime state.
window.createRuntimeInspector = function createRuntimeInspector(api) {
  const $ = selector => document.querySelector(selector);
  const LIMIT = 4096;
  const node = (tag,text,className) => {
    const result=document.createElement(tag);
    if(text!==undefined) result.textContent=text;
    if(className) result.className=className;
    return result;
  };
  const empty = () => ({rows:[],more:false,busy:false,error:'',sample:null,ticket:0});
  let fields=empty(), instances=empty();
  let pid=null, room=null, capable=false, visible=false, mode='globals', selected=null, generation=0;
  let notice='Connect to a runtime to inspect variables. No mod folder is required.';
  const available=()=>pid!==null && capable;
  function clear(message) {
    ++generation;
    fields=empty(); instances=empty(); selected=null;
    notice=message;
    render();
  }
  function sampleAge() {
    const descriptions=[];
    for(const [label,state] of [['Fields',fields],['Instances',instances]]) {
      if(!state.sample) continue;
      const age=Math.max(0,Math.round(state.sample.ageMs+performance.now()-state.sample.received));
      descriptions.push(`${label}: oldest loaded page ${age.toLocaleString()} ms ago (${new Date(state.sample.sampledAt).toLocaleTimeString()})`);
    }
    $('#runtime-sample-age').textContent=descriptions.join(' · ') || 'No variable sample';
  }
  function stateText(state,count,label) {
    if(state.error) return `Request failed: ${state.error}. Previous ${label} cleared. Refresh to retry.`;
    if(state.busy) return `Loading ${label}…${state.rows.length ? ` ${state.rows.length} previously loaded rows.` : ''}`;
    if(!state.sample) return label==='fields' && mode==='instances' && selected===null ? 'Select an instance to inspect its fields.' : `Refresh to load ${label}.`;
    const ending=state.more ? state.rows.length>=LIMIT ? 'Loaded-row limit reached; additional rows not displayed.' : 'Partial list — more rows available.' : 'End of reported list.';
    return `${count} shown / ${state.rows.length} loaded. ${ending}`;
  }
  function renderFields() {
    const query=$('#runtime-field-filter').value.toLowerCase();
    const shown=fields.rows.filter(item=>[item.name,item.type,item.source,item.value].some(value=>String(value).toLowerCase().includes(query)));
    const body=$('#runtime-fields tbody'); body.replaceChildren();
    for(const item of shown) {
      const row=node('tr');
      for(const key of ['name','type','source','value']) {
        const cell=node('td',item[key]);
        if(key==='value') cell.className='runtime-value';
        row.append(cell);
      }
      body.append(row);
    }
    $('#runtime-field-title').textContent=mode==='globals' ? 'Global variables' : selected===null ? 'Instance fields' : `Instance ${selected}`;
    $('#runtime-field-status').textContent=stateText(fields,shown.length,'fields');
    $('#runtime-field-status').classList.toggle('runtime-error',!!fields.error);
    $('#runtime-refresh-fields').disabled=!available() || fields.busy || (mode==='instances' && selected===null);
    $('#runtime-more-fields').hidden=!fields.more;
    $('#runtime-more-fields').disabled=!available() || fields.busy || fields.rows.length>=LIMIT;
  }
  function renderInstances() {
    const query=$('#runtime-instance-filter').value.toLowerCase();
    const shown=instances.rows.filter(item=>`${item.object} ${item.id}`.toLowerCase().includes(query));
    const groups=new Map();
    for(const item of shown) {
      if(!groups.has(item.object)) groups.set(item.object,[]);
      groups.get(item.object).push(item);
    }
    const host=$('#runtime-object-groups'); host.replaceChildren();
    for(const [object,items] of groups) {
      const group=node('details'); group.open=true;
      group.append(node('summary',`${object} (${items.length} loaded)`));
      for(const item of items) {
        const button=node('button',`#${item.id}`,selected===item.id ? 'selected' : '');
        button.dataset.instanceId=String(item.id);
        button.setAttribute('aria-label',`Inspect instance ${item.id}`);
        button.setAttribute('aria-pressed',String(selected===item.id));
        button.onclick=()=>{
          selected=item.id; fields=empty(); $('#runtime-field-filter').value='';
          // Replacing the state object invalidates pending requests for the previous target.
          render(); request('fields');
        };
        group.append(button);
      }
      host.append(group);
    }
    $('#runtime-instance-status').textContent=stateText(instances,shown.length,'instances');
    $('#runtime-instance-status').classList.toggle('runtime-error',!!instances.error);
    $('#runtime-refresh-instances').disabled=!available() || instances.busy;
    $('#runtime-more-instances').hidden=!instances.more;
    $('#runtime-more-instances').disabled=!available() || instances.busy || instances.rows.length>=LIMIT;
  }
  function render() {
    $('#runtime-context').textContent=pid===null ? 'No process attached' : `Process ${pid} · Room ${room ?? 'unknown'}`;
    $('#runtime-notice').textContent=notice;
    $('#runtime-notice').hidden=!notice;
    $('#runtime-content').hidden=!available();
    $('#runtime-instance-browser').hidden=mode!=='instances';
    $('#runtime-content').classList.toggle('with-instances',mode==='instances');
    for(const kind of ['globals','instances']) {
      $(`#runtime-${kind}`).classList.toggle('active',mode===kind);
      $(`#runtime-${kind}`).setAttribute('aria-pressed',String(mode===kind));
    }
    renderFields(); renderInstances(); sampleAge();
  }
  async function request(section,append=false) {
    if(!available()) return;
    if(section==='fields' && mode==='instances' && selected===null) return;
    let state=section==='fields' ? fields : instances;
    if(state.busy || (append && (!state.more || state.rows.length>=LIMIT))) return;
    if(!append) {
      state=empty();
      if(section==='fields') fields=state;
      else { instances=state; fields=empty(); selected=null; }
    }
    const kind=section==='instances' ? 'instances' : mode==='globals' ? 'globals' : 'instance';
    const target=kind==='instance' ? selected : null;
    const query={kind,offset:append ? state.rows.length : 0};
    if(kind==='instance') query.instanceId=target;
    const currentGeneration=generation, currentPid=pid, currentRoom=room, ticket=++state.ticket;
    const current=()=>generation===currentGeneration && pid===currentPid && room===currentRoom &&
      state===(section==='fields' ? fields : instances) && state.ticket===ticket;
    state.busy=true; state.error=''; notice=''; render();
    try {
      const result=await api.inspect(currentPid,query);
      if(!current()) return;
      if(result.error!==null) throw new Error(result.error || 'The bridge reported an inspection error');
      if(result.room!==currentRoom) {
        clear('Room changed during inspection. Values cleared; refresh after telemetry updates.');
        return;
      }
      if(result.protocol!==2 || result.pid!==currentPid || result.kind!==kind || result.target!==target ||
        result.offset!==query.offset || !Array.isArray(result.items) || result.items.length>64 ||
        (result.hasMore && !result.items.length)) throw new Error('Invalid inspection page');
      const remaining=LIMIT-state.rows.length;
      state.rows.push(...result.items.slice(0,remaining));
      state.more=result.hasMore || result.items.length>remaining;
      // Keep the oldest page's age so appending never makes older values look fresh.
      if(!state.sample) state.sample={sampledAt:result.sampledAt,ageMs:result.ageMs,received:performance.now()};
    } catch(error) {
      if(!current()) return;
      state.rows=[]; state.more=false; state.sample=null; state.error=error.message || String(error);
      if(section==='instances') { fields=empty(); selected=null; }
    } finally {
      if(current()) { state.busy=false; render(); }
    }
  }
  function changeMode(next) {
    if(mode===next) return;
    mode=next;
    clear(available() ? '' : notice); $('#runtime-field-filter').value=''; $('#runtime-instance-filter').value='';
    request(mode==='globals' ? 'fields' : 'instances');
  }
  $('#runtime-globals').onclick=()=>changeMode('globals');
  $('#runtime-instances').onclick=()=>changeMode('instances');
  $('#runtime-field-filter').oninput=renderFields;
  $('#runtime-instance-filter').oninput=renderInstances;
  $('#runtime-refresh-fields').onclick=()=>request('fields');
  $('#runtime-more-fields').onclick=()=>request('fields',true);
  $('#runtime-refresh-instances').onclick=()=>request('instances');
  $('#runtime-more-instances').onclick=()=>request('instances',true);
  // This timer only ages the displayed sample. Inspection is never polled.
  const ageTimer=setInterval(()=>{ if(visible) sampleAge(); },1000);
  window.addEventListener('beforeunload',()=>clearInterval(ageTimer));
  render();
  return {
    setVisible(value) {
      visible=value;
      if(value && available() && !fields.sample && !instances.sample && !fields.busy && !instances.busy && !fields.error && !instances.error)
        request(mode==='globals' ? 'fields' : 'instances');
    },
    snapshot(sample) {
      const wasConnected=pid!==null;
      const nextCapability=sample.capabilities?.inspectVariables===true && typeof api.inspect==='function';
      const changed=sample.pid!==pid || sample.room!==room || nextCapability!==capable;
      pid=sample.pid; room=sample.room; capable=nextCapability;
      if(!changed) return;
      clear(!capable ? 'Connector update needed: this bridge does not support variable inspection. Update RunnerEditor and reconnect.' :
        wasConnected ? 'Runtime context changed. Previous values cleared. Refresh to inspect this room.' : '');
      if(!wasConnected && visible && capable) request(mode==='globals' ? 'fields' : 'instances');
    },
    disconnect(reason='Disconnected') {
      pid=null; room=null; capable=false;
      clear(`${reason}. Connect to a runtime to inspect variables. No mod folder is required.`);
    }
  };
};
