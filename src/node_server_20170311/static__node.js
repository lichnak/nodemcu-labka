window.$=function(s,c){var r=null,e,g="getElement",m={"#":g+"ById",".":g+"sByClassName","@":g+"sByName","=":g+"sByTagName","*":"querysAll"}[s[0]];if(m){e=((c?c:document)[m](s.slice(1)));r=(e&&e.length<2)?e[0]:e;};return r;};
var ajax=function(p,c){var h=p.headers||{},b=p.body,m=p.method||(b?"POST":"GET"),d=false,r=new XMLHttpRequest();

function cb(s,rt){return function(){if(!d){c(r.status===undefined?s:r.status,r.status===0?"Error":(r.response||r.responseText||rt),r);d=true;}};}
r.open(m,p.url,true);var sc=r.onload=cb(200);r.onreadystatechange=function(){if(r.readyState===4){sc();}};r.ontimeout=r.onabort=r.onerror=cb(null,"Error");
h["Content-Type"]=h["Content-Type"]||"application/x-www-form-urlencoded";for(f in h){r.setRequestHeader(f,h[f]);}r.send(b);return r;}
function updateElement(e,v,s){if(e){var tn=e.tagName;if(typeof v=="object"){for(k in v){if(v.hasOwnProperty(k)){var vk=v[k];;
if(k=="_"){if(tn=="path"||tn=="rect"){e.setAttribute("data-v",vk);}else if(tn=="text"){e.textContent=vk;}else{e.innerHTML=vk;}}
else if(k=="style"&&typeof vk=="object"){for(i in vk){if(vk.hasOwnProperty(i)){e.style[i]=vk[i];}}}else{e.setAttribute(k,vk);}}}}
else if(tn=="INPUT"||tn=="BUTTON"){if(e.type=="checkbox"){e.checked=(v==true||v=="true")?true:false}else{e.value=v;}}
else if(e.hasAttribute("src")&&v.match(/^(http|\/)/)){e.src=v;}
else if(tn=="text"){e.textContent=v;}
else{if(s&&v!=e.innerHTML){e.style.fontWeight="bold";setTimeout(function(){e.style.fontWeight="normal";},4000);}e.innerHTML=v;}}}
function jsonUpdate(r,s){try{var j=JSON.parse(r);if(!j){return;}for(k in j){if(j.hasOwnProperty(k)){updateElement($(k),j[k],s)}}}catch(e){console.log(e)}}
function ajaxRefresh(u,m,b,i,s){var ax=function(){ajax({url:u,method:m,body:b},function(code,rs,rq){if(code==200&&rs){jsonUpdate(rs,s)}setTimeout(ax,i);})};ax();}
function collectData(l){var s=l.split(","),o="",u=encodeURIComponent;for(var i=0;i<s.length;i++){var e=$(s[i]);if(e){o+=((o!="")?"&":"")+u(e.name||e.id||e.tagName)+"="+u(e.type=="checkbox"?e.checked:e.value||e.innerHTML||"");}}return o;}
function bindClick(e,u,ec){var el=$(e);if(el){el.addEventListener("click",function(){if(!ec){ec=(this.id&&"#"+this.id)||(this.name&&"@"+this.name);}ajax({url:u,method:"POST",body:collectData(ec)},function(code,rs,rq){if(code==200&&rs){jsonUpdate(rs)}});})}}
function HTMLEncode(s){if(s){var i=s.length,r=[];while(i--){var c=s[i].charCodeAt();r[i]=s[i];if(c==10){r[i]="<br />";}else if(c<65||c>127||(c>90&&c<97)){r[i]='&#'+c+';';}}return r.join('');}return s;}
function gpbn(n){var rx=new RegExp("[?&]"+n.replace(/[\[\]]/g,"\\$&")+"(=([^&#]*)|&|#|$)"),r=rx.exec(window.location.href);return decodeURIComponent((r&&r[2]||'').replace(/\+/g," "));}
(typeof ready=="function")&&ready("lib_main");
