// ============================================================
// UI.h  —  Web control panel HTML (ESP32)
// ============================================================
#pragma once
#include <Arduino.h>

inline String UI() {
    String html;
    html.reserve(5000);

    html += F("<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>RC Tank</title><style>"
        "body{font-family:Arial,sans-serif;background:#111;color:#fff;padding:10px;text-align:left;}"
        "table{width:100%;border-collapse:collapse;}"
        "table,th,td{border:1px solid #fff;text-align:center;padding:10px;}"
        "td{font-size:22px;}"
        "button{width:100%;height:60px;font-size:20px;border:none;cursor:pointer;border-radius:4px;}"
        "input[type=range]{width:100%;}"
        "</style></head><body>");

    html += F("<table><tr>"
        "<td colspan='3' style='color:#0c0;font-weight:bold'>RC Tank Controller</td>"
        "</tr><tr>");
    html += F("<td><button id='startID' style='background:#E7F527'>Start</button></td>"
        "<td><button id='pauseID' style='background:#E7F527'>Pause</button></td>"
        "<td><button id='estopID' style='background:#c00;color:#fff'>E-STOP</button></td>"
        "</tr><tr>");
    html += F("<td>Steering</td>"
        "<td colspan='2'><input type='range' id='steerSlider' min='0' max='100' value='50' step='5' "
        "onchange='onSteer(this.value)'/></td>"
        "</tr><tr>");
    html += F("<td>Direction</td>"
        "<td><button id='forwardID'  style='background:#0c0'>Forward</button></td>"
        "<td><button id='backwardID' style='background:#E7F527'>Backward</button></td>"
        "</tr><tr>");
    html += F("<td>Speed</td>"
        "<td colspan='2'><input type='range' id='speedSlider' min='0' max='100' value='0' step='5' "
        "onchange='onSpeed(this.value)'/></td>"
        "</tr><tr>");
    html += F("<td><button id='refreshID'    style='background:#E7F527'>Refresh</button></td>"
        "<td><button id='lightToggleID' style='background:#fa0'>Lights: ON</button></td>"
        "<td id='statusCell' style='font-size:14px;color:#aaa'>--</td>"
        "</tr><tr>");
    html += F("<td><button id='startLogID' style='background:#E7F527'>Start Log</button></td>"
        "<td><button id='viewLogID'  style='background:#E7F527'>View Log</button></td>"
        "<td><button id='clearLogID' style='background:#E7F527'>Clear Log</button></td>"
        "</tr></table>");
    html += F("<div id='logDiv' style='display:none;margin-top:8px;background:#1a1a1a;"
        "border:1px solid #555;padding:8px;max-height:250px;overflow-y:auto;'>"
        "<pre id='logPre' style='color:#0f0;font-size:12px;margin:0;"
        "white-space:pre-wrap;word-break:break-all;'></pre></div>");

    // ── JavaScript ────────────────────────────────────────────
    html += F("<script>");
    html += F(
        "const btnStart      =document.getElementById('startID');"
        "const btnPause      =document.getElementById('pauseID');"
        "const btnEstop      =document.getElementById('estopID');"
        "const btnForward    =document.getElementById('forwardID');"
        "const btnBackward   =document.getElementById('backwardID');"
        "const btnRefresh    =document.getElementById('refreshID');"
        "const btnLightToggle=document.getElementById('lightToggleID');"
        "const btnStartLog   =document.getElementById('startLogID');"
        "const btnViewLog    =document.getElementById('viewLogID');"
        "const btnClearLog   =document.getElementById('clearLogID');"
        "const steerSlider   =document.getElementById('steerSlider');"
        "const speedSlider   =document.getElementById('speedSlider');"
        "const statusCell    =document.getElementById('statusCell');"
        "const logDiv        =document.getElementById('logDiv');"
        "const logPre        =document.getElementById('logPre');"
        "const YELLOW='#E7F527',GREEN='#0c0',RED='#c00',ORANGE='#fa0';"
    );
    html += F(
        "let appState='initial',savedState=null,direction=1;"
        "let lightsOn=true,lastLightMode='';"

        "function api(url){fetch(url).catch(()=>{});}"

        // Auto turn signals: threshold steering values match RULES.md
        "function updateLights(){"
            "let mode;"
            "if(!lightsOn){mode='off';}"
            "else{"
                "const s=parseFloat(steerSlider.value);"
                "if(s<40)mode='left';"
                "else if(s>60)mode='right';"
                "else mode='full';"
            "}"
            "if(mode!==lastLightMode){lastLightMode=mode;api('/api/lights?mode='+mode);}"
            "btnLightToggle.textContent=lightsOn?'Lights: ON':'Lights: OFF';"
            "btnLightToggle.style.backgroundColor=lightsOn?ORANGE:YELLOW;"
        "}"

        "function sendDrive(){"
            "const spd=parseFloat(speedSlider.value)/100*direction;"
            "const str=(parseFloat(steerSlider.value)-50)/50;"
            "api('/api/drive?speed='+spd.toFixed(2)+'&steer='+str.toFixed(2));"
            "updateLights();"
        "}"

        "function onSpeed(v){sendDrive();}"
        "function onSteer(v){sendDrive();}"
    );
    html += F(
        "function saveState(){"
            "savedState={forward:btnForward.style.backgroundColor,"
                "speed:speedSlider.value,steer:steerSlider.value};"
        "}"

        "function resetToInitial(){"
            "btnForward.style.backgroundColor=GREEN;"
            "btnBackward.style.backgroundColor=YELLOW;"
            "speedSlider.value=0;steerSlider.value=50;"
            "direction=1;api('/api/drive?speed=0&steer=0');"
            "updateLights();"
        "}"

        "function restoreState(){"
            "if(!savedState){resetToInitial();return;}"
            "btnForward.style.backgroundColor=savedState.forward;"
            "btnBackward.style.backgroundColor=(savedState.forward===GREEN)?YELLOW:GREEN;"
            "speedSlider.value=savedState.speed;"
            "steerSlider.value=savedState.steer;"
            "direction=(savedState.forward===GREEN)?1:-1;"
            "sendDrive();savedState=null;"
        "}"
    );
    html += F(
        "btnStart.addEventListener('click',()=>{"
            "if(appState==='paused'){api('/api/resume');restoreState();}"
            "else{api('/api/start');savedState=null;resetToInitial();}"
            "btnStart.style.backgroundColor=GREEN;"
            "btnPause.style.backgroundColor=YELLOW;"
            "appState='running';"
        "});"

        "btnPause.addEventListener('click',()=>{"
            "if(appState==='paused'){"
                "api('/api/resume');restoreState();"
                "btnPause.style.backgroundColor=YELLOW;"
                "btnStart.style.backgroundColor=GREEN;"
                "appState='running';"
            "}else if(appState==='running'){"
                "saveState();api('/api/pause');"
                "btnPause.style.backgroundColor='#00f';"
                "btnStart.style.backgroundColor=YELLOW;"
                "appState='paused';"
            "}"
        "});"

        "btnForward.addEventListener('click',()=>{"
            "direction=1;"
            "btnForward.style.backgroundColor=GREEN;"
            "btnBackward.style.backgroundColor=YELLOW;"
            "sendDrive();"
        "});"

        "btnBackward.addEventListener('click',()=>{"
            "direction=-1;"
            "btnBackward.style.backgroundColor=GREEN;"
            "btnForward.style.backgroundColor=YELLOW;"
            "sendDrive();"
        "});"

        "btnEstop.addEventListener('click',()=>{"
            "if(appState==='estopped'){"
                "api('/api/start');savedState=null;resetToInitial();"
                "btnEstop.style.backgroundColor=RED;"
                "btnStart.style.backgroundColor=YELLOW;"
                "appState='initial';"
            "}else{"
                "api('/api/estop');"
                "lightsOn=false;lastLightMode='';updateLights();"
                "btnEstop.style.backgroundColor='#ff0';"
                "appState='estopped';"
            "}"
        "});"

        "btnLightToggle.addEventListener('click',()=>{"
            "lightsOn=!lightsOn;"
            "updateLights();"
        "});"
    );
    html += F(
        "btnRefresh.addEventListener('click',syncStatus);"

        "function syncStatus(){"
            "fetch('/api/status').then(r=>r.json()).then(d=>{"
                "if(d.emergencyStopped){"
                    "statusCell.textContent='E-STOPPED';"
                    "statusCell.style.color='#f00';"
                "}else{"
                    "statusCell.textContent='OK';"
                    "statusCell.style.color='#0f0';"
                "}"
            "}).catch(()=>{"
                "statusCell.textContent='offline';"
                "statusCell.style.color='#f00';"
            "});"
        "}"

        "setInterval(syncStatus,2000);"
        "syncStatus();"
    );
    html += F(
        "btnStartLog.addEventListener('click',()=>{"
            "api('/api/startlog');"
            "btnStartLog.textContent='Logging';"
            "btnStartLog.style.backgroundColor=GREEN;"
            "btnStartLog.disabled=true;"
        "});"

        "btnViewLog.addEventListener('click',()=>{"
            "fetch('/api/log')"
                ".then(r=>r.text())"
                ".then(t=>{"
                    "logPre.textContent=t||'(empty)';"
                    "logDiv.style.display='block';"
                    "logDiv.scrollTop=logDiv.scrollHeight;"
                "})"
                ".catch(()=>{"
                    "logPre.textContent='(fetch error)';"
                    "logDiv.style.display='block';"
                "});"
        "});"

        "btnClearLog.addEventListener('click',()=>{"
            "api('/api/clearlog');"
            "logPre.textContent='';"
            "logDiv.style.display='none';"
        "});"
    );
    html += F("</script></body></html>");

    return html;
}
