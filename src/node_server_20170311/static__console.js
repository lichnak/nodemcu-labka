var cmdList=[
['Heap','=node.heap()'],
['Chip Info','local mav,miv,dev,cid,fid,fs,fm,fsp=node.info();return "Version: "..mav.."."..miv.."."..dev.."\\nChip Id: "..cid.."\\nFlash Id: "..fid.."\\nFlash size: "..fs.."\\nFlash mode: "..fm.."\\nFlash speed: "..fsp'],
['Chip Id','=node.chipid()'],
['Flash Id','=node.flashid()'],
['FS Info','local r,u,t=file.fsinfo();return "File system info:\\nTotal: "..t.." (k)Bytes\\nUsed : "..u.." (k)Bytes\\nRemain: "..r.." (k)Bytes"'],
['File List','local l,o=file.list(),"";for k,v in pairs(l)do o=o..k.." ("..v..")\\n" end;return o'],
['Flash size','=node.flashsize()'],
['Boot reason','local rc,rv=node.bootreason();return "code: "..rc.."\\ncause: "..rv'],
['WiFi STA get status','local st={"IDLE","CONNECTING","STA_WRONGPWD","STA_APNOTFOUND","FAIL","GOTIP"};return st[wifi.sta.status()+1]'],
['WiFi STA get MAC','=wifi.sta.getmac()'],
['WiFi STA get RSSI','=wifi.sta.getrssi()'],
['WiFi STA get IP','=wifi.sta.getip()'],
['WiFi AP get MAC','=wifi.ap.getmac()'],
['WiFi AP get IP','=wifi.ap.getip()'],

];
var cnt=document.getElementById("remcmd");
if (cnt){
  for (var i=0;i<cmdList.length;i++){
    var button=document.createElement("button");
    button.className="btn-b";
    button.type="submit";
    button.value=cmdList[i][0];
    button.innerHTML=cmdList[i][0];
    button.setAttribute("data",cmdList[i][1]);
    button.style.margin="5px";
    cnt.appendChild(button);
    button.addEventListener("click",function(event){
      console.log(this.getAttribute("data"));
      sendCmd(this.getAttribute("data"));
    })

  }
}



