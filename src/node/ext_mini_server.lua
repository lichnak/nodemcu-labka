serverService=0
serverRouting={GET={},HEAD={},POST={},PUT={},DELETE={},OPTIONS={}}
serverStatic="/html/"
function serverOn(_m,_p,_c)local m=string.upper(tostring(_m));if serverRouting[m]then serverRouting[m][tostring(_p)]=_c end end
function serverParseQuery(_q)
    local d={}
    for k,v in string.gmatch(_q,"%s*&?([^=]+)=([^&]+)")do d[k]=tostring(v):gsub("%%(%x%x)",function(x)return string.char(tonumber(x,16))end)end
    return d
end
function serverParseRequest(_p)
    if string.len(_p)>2048 then return 503 end
    local ls,le,m,r=_p:find("^([A-Z]+) (.-) HTTP/")
    if ls~=1 then return 400 end
    if not serverRouting[m] then return 501 end
    local u,q=string.match(r,"^([^%?]+)%?*(.*)$")
    if m=="GET" and string.find(u,tostring(serverStatic))then return nil,true,u end
    q=tostring(q):gsub("&amp;","&"):gsub("%+"," ")
    local req={method=m,request=r,uri=u,query=q,get=serverParseQuery(q)}
    local h,b=string.match(_p,"^(.*)\r\n\r\n(.*)$")
    _p=nil
    if h then for k,v in string.gmatch(h,"\n([^:]+):%s*([^\n]+)") do req[k]=v end h=nil end
    if b then
        local ct=req["Content-Type"]
        if ct=="application/x-www-form-urlencoded" then req.post=serverParseQuery(b)
        elseif ct=="application/json" then req.json=cjson.decode(b)
        end
        b=nil
    end
    return nil,nil,req
end
function serverSend(_c,_s,_t,_h,_d)
    local r={[200]="OK",[204]="No Content",[302]="Found",[304]="Not Modified",[400]="Bad Request",[401]="Unauthorized",[403]="Forbidden",[404]="Not Found",[500]=" Internal Server Error",[501]="Not Implemented",[503]="Service Unavailable"}
    _c:send(table.concat({
        "HTTP/1.1 ",_s," ",(r[_s] or "Unknown"),"\r\n",
        (type(_h)=="string" and _h or ""),
        (type(_t)=="string" and "Content-Type: ".._t.."\r\n" or ""),
        "Content-Length: "..(_d and _d~="" and string.len(_d).."\r\n\r\n".._d.."\r\n" or "0\r\n\r\n")
    }))
    _c:close()
    collectgarbage("collect")
end
function serverSendFile(_c,_u)
    local fp=string.sub(_u,2)
    local m=({css="text/css",txt="text/plain",html="text/html",js="application/javascript",json="application/json"})[tostring(string.match(fp,"%.(%S+)$"))] or "text/plain"
    local enc=""
    if file.open(fp..".gz")then enc="Content-Encoding: gzip\r\n"
    elseif file.open(fp) then
    else serverSend(_c,404)do return end
    end
    serverSend(_c,200,m,enc,file.read())
    file.close()
end
function serverRequest(_c,_r)
    local s,st,req=serverParseRequest(_r)
    _r=nil
    if s  then serverSend(_c,s)do return end end
    if st then serverSendFile(_c,req)do return end end
    cb=serverRouting[req.method][req.uri]
    if not cb then serverSend(_c,404)do return end end
    local su,err=pcall(function()
        local cbs,ct,cont=cb(req)
        if cbs then
            serverSend(_c,cbs,ct,"",cont)
            cont=nil
        else
            serverSend(_c,204)
        end
    end)
    if not su then serverSend(_c,500,"text/plain","",err)end
end
function serverStart(_p)
    for k,v in pairs(file.list()) do p,n=string.match(k,"^(.*)__(.*)$");if p then file.rename(k,p.."/"..n) end end
    local p=_p and tonumber(_p) or 80
    print("Starting Web Server - port: ",p)
    if serverService~=0 then serverService:close()end
    serverService=net.createServer(net.TCP,30)
    serverService:listen(p,function(_c)_c:on("receive",serverRequest)end)
    return serverService
end
