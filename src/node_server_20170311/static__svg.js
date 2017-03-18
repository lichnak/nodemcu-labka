function aM(a){return["id","style","width","height","data-t","data-n","data-x","path","rect"][a]||a}
function pT(c,w){return"fill:none;stroke:"+c+";stroke-width:"+w}
function aR(x,y,r,u,w){var m=Math,d=m.PI/180.0,a=(w-90)*d,b=(u-90)*d;return["M",x+r*m.cos(a),y+r*m.sin(a),"A",r,r,0,w-u>180?1:0,0,x+r*m.cos(b),y+r*m.sin(b)].join(" ")}
function aT(u,w){return aR(110,100,70,u,w)}
function mP(x,a,b,c,d){return((a>x?a:x>b?b:x)-a)*(d-c)/(b-a)+c}
function mS(p,e,a,h,s){s=document.createElementNS("http://www.w3.org/2000/svg",aM(e));for(i in a){s.setAttribute(aM(i),a[i]);};h&&(s.innerHTML=h);p&&p.appendChild(s);return s}
function sG(p,w,h,u,v){return mS(mS(p,"svg",{2:w,3:h,viewBox:"0 0 "+u+" "+v}),"g")}
function sT(g,i,x,y,s,a,t,c){mS(g,"text",{0:i,x:x,y:y,"text-anchor":["start","middle","end"][a],1:"font-size:"+s+";fill:"+c},t||"")}
function sP(g,i,d,c,w,t,n,x){return mS(g,7,{0:i,d:d,1:pT(c,w),4:t,5:n,6:x})}
function sA(e,a,v){e.setAttribute(a,v)}
window.MOBSA={"a":function(e,v,l,h){sA(e,"d",aT(-90,mP(v,l,h,-90,90)))},"c":function(e,v,l,h){sA(e,"d",aT(0,mP(v,l,h,0,359.99)))},"t":function(e,v,l,h){var g=mP(v,l,h,0,200);sA(e,"height",g);sA(e,"y",210-g)},}
window.MOBS=window.MutationObserver||window.WebKitMutationObserver||window.MozMutationObserver;
var obs=new MOBS(function(s){s.forEach(function(m){var e=m.target;if(e&&m.attributeName=="data-v"){e.gA=e.getAttribute;var f=parseFloat,t=e.gA("data-t"),v=e.gA("data-v"),l=f(e.gA("data-n")||0),h=f(e.gA("data-x")||100);if(MOBSA[t]){MOBSA[t](e,v,l,h);}}})});
function svgBuild(d,t,p){
var e=$(d),q=p.id||"id"+0|Math.random()*1e5,cl=p.color||"#eee",a=220,b=100,c=110,w=p.width||a,h=p.height||a,n=p.min||0,x=p.max||b,f1="20px",f2="30px",tc="#555",tg="#bbb",f="fill:";
for(i=26;i--;){var z=String.fromCharCode(97+i);window["q"+z]=q+z}if(e){var sv,g=sG(e,w,h,a,a);
if(t=="a"){sT(g,qt,c,b,f2,1,0,tc);sT(g,qd,c,130,f1,1,0,tc);sP(g,qb,aT(-90,90),"#eee",20);sv=sP(g,qv,aT(-90,-90),cl,20,t,n,x)}
if(t=="c"){sT(g,qt,c,c,f2,1,0,tc);sT(g,qd,c,200,f1,1,0,tc);sP(g,qb,aT(0,359.99),"#eee",20);sv=sP(g,qv,aT(0,0),cl,20,t,n,x)}
if(t=="b"){mS(g,7,{0:qv,1:f+cl,d:"M110,25C92,25 75,37 75,59c0,24 15,27 17,57 l 33,0c4,-30 17,-36 17,-57 0,-21 -14,-34 -34,-34z"});mS(g,7,{0:qr,1:f+"#999",d:"m98,143 21,0c-2,9 -18,9 -21,0z"});for(u=3;u--;){mS(g,8,{0:q+u,1:f+tg,2:30,3:5,x:94,y:[119,127,135][u]})}sT(g,qd,110,200,f1,1,0,tc);}
if(t=="t"){mS(g,"rect",{0:qj,1:f+"#eee",2:20,3:200,x:20,y:10});sv=mS(g,8,{0:qv,1:f+cl,2:20,x:20,4:t,5:n,6:x});sT(g,qt,130,120,f2,1,0,tc);sT(g,qd,130,150,f1,1,0,tc);sT(g,qx,45,25,f1,0,0,tg);sT(g,qn,45,210,f1,0,0,tg);sP(g,qh,"m20,10 20,0m-20,200 20,0","#aaa",2);if(n<0&&x>0){sP(g,ql,"m20,"+(210-mP(0,n,x,0,200))+" 20,0","#fff",2)}}
sv&&obs.observe(sv,{attributes:!0})}}
(typeof ready=="function")&&ready("lib_svg");