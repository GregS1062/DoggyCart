// ============================================================
// ui.h  —  Web control panel HTML (RPi5)
// ============================================================
#pragma once
#include <string>
using String = std::string;
#define F(x) (x)

inline String UI() {
    String html;
    html.reserve(6500);

    html += F("<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>DoggyCart</title><style>"
        "body{font-family:Arial,sans-serif;background:#111;color:#fff;padding:10px;text-align:left;}"
        "table{width:100%;border-collapse:collapse;}"
        "table,th,td{border:1px solid #fff;text-align:center;padding:10px;}"
        "td{font-size:22px;}"
        "button{width:100%;height:60px;font-size:20px;border:none;cursor:pointer;border-radius:4px;}"
        "input[type=range]{width:100%;}"
        "</style></head><body>");

    html += F("<table><tr>"
        "<td colspan='3' style='color:#0c0;font-weight:bold'>DoggyCart Controller</td>"
        "</tr><tr>");
    html += F("<td><button id='startID'   style='background:#E7F527'>Start</button></td>"
        "<td><button id='pauseID'   style='background:#E7F527'>Pause</button></td>"
        "<td><button id='estopID'   style='background:#c00;color:#fff'>E-STOP</button></td>"
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
    html += F("<td><button id='trackID'   style='background:#E7F527'>Track</button></td>"
        "<td><button id='photoID'   style='background:#E7F527'>Photo</button></td>"
        "<td><button id='refreshID' style='background:#E7F527'>Refresh</button></td>"
        "</tr><tr>");
    html += F("<td><button id='startLogID' style='background:#E7F527'>Start Log</button></td>"
        "<td><button id='viewLogID'  style='background:#E7F527'>View Log</button></td>"
        "<td><button id='clearLogID' style='background:#E7F527'>Clear Log</button></td>"
        "</tr></table>");
    html += F("<div id='statusCell' style='margin:4px 0;font-size:13px;color:#aaa;text-align:right;'>--</div>");
    html += F("<div id='photoDiv' style='display:none;margin-top:8px;background:#1a1a1a;"
        "border:1px solid #555;padding:8px;overflow-x:auto;'>"
        "<div id='photoRow' style='display:flex;flex-wrap:wrap;gap:4px;'></div></div>");
    html += F("<div id='logDiv' style='display:none;margin-top:8px;background:#1a1a1a;"
        "border:1px solid #555;padding:8px;max-height:250px;overflow-y:auto;'>"
        "<pre id='logPre' style='color:#0f0;font-size:12px;margin:0;"
        "white-space:pre-wrap;word-break:break-all;'></pre></div>");

    // ── JavaScript ────────────────────────────────────────────
    html += F("<script>");
    html += F(
        "const btnStart    =document.getElementById('startID');"
        "const btnPause    =document.getElementById('pauseID');"
        "const btnEstop    =document.getElementById('estopID');"
        "const btnTrack    =document.getElementById('trackID');"
        "const btnPhoto    =document.getElementById('photoID');"
        "const btnForward  =document.getElementById('forwardID');"
        "const btnBackward =document.getElementById('backwardID');"
        "const btnRefresh  =document.getElementById('refreshID');"
        "const btnStartLog =document.getElementById('startLogID');"
        "const btnViewLog  =document.getElementById('viewLogID');"
        "const btnClearLog =document.getElementById('clearLogID');"
        "const steerSlider =document.getElementById('steerSlider');"
        "const speedSlider =document.getElementById('speedSlider');"
        "const statusCell  =document.getElementById('statusCell');"
        "const photoDiv    =document.getElementById('photoDiv');"
        "const photoRow    =document.getElementById('photoRow');"
        "const logDiv      =document.getElementById('logDiv');"
        "const logPre      =document.getElementById('logPre');"
        "const YELLOW='#E7F527',GREEN='#0c0',RED='#c00';"
    );
    html += F(
        "let appState='initial',savedState=null,direction=1,tracking=false;"

        "function api(url){fetch(url).catch(()=>{});}"

        "function sendDrive(){"
            "const spd=parseFloat(speedSlider.value)/100*direction;"
            "const str=(parseFloat(steerSlider.value)-50)/50;"
            "api('/api/drive?speed='+spd.toFixed(2)+'&steer='+str.toFixed(2));"
        "}"

        "function onSpeed(v){if(!tracking)sendDrive();}"
        "function onSteer(v){if(!tracking)sendDrive();}"
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
                "btnEstop.style.backgroundColor='#ff0';"
                "if(tracking){"
                    "tracking=false;"
                    "btnTrack.style.backgroundColor=YELLOW;"
                    "api('/api/track?on=0');"
                "}"
                "appState='estopped';"
            "}"
        "});"
    );
    html += F(
        "btnTrack.addEventListener('click',()=>{"
            "tracking=!tracking;"
            "if(tracking){"
                "api('/api/track?on=1');"
                "btnTrack.style.backgroundColor=GREEN;"
            "}else{"
                "api('/api/track?on=0');"
                "btnTrack.style.backgroundColor=YELLOW;"
                "sendDrive();"
            "}"
        "});"

        "btnPhoto.addEventListener('click',()=>{"
            "if(photoDiv.style.display!=='none'){photoDiv.style.display='none';return;}"
            "fetch('/api/photos')"
                ".then(r=>r.json())"
                ".then(d=>{"
                    "photoRow.innerHTML='';"
                    "if(d.photos&&d.photos.length>0){"
                        "d.photos.forEach(url=>{"
                            "const a=document.createElement('a');"
                            "a.href=url;a.target='_blank';"
                            "const img=document.createElement('img');"
                            "img.src=url;"
                            "img.style.cssText='width:100px;height:75px;object-fit:cover;margin:2px;';"
                            "a.appendChild(img);photoRow.appendChild(a);"
                        "});"
                    "}else{"
                        "photoRow.textContent='(no photos)';"
                    "}"
                    "photoDiv.style.display='block';"
                "})"
                ".catch(()=>{"
                    "photoRow.textContent='(fetch error)';"
                    "photoDiv.style.display='block';"
                "});"
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
                "if(d.loggingEnabled){"
                    "btnStartLog.textContent='Logging';"
                    "btnStartLog.style.backgroundColor=GREEN;"
                    "btnStartLog.disabled=true;"
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
