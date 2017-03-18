serverService=0
serverRouting={GET={},POST={}}
function serverOn(_m,_p,_c)local m=string.upper(tostring(_m));if serverRouting[m]then serverRouting[m][tostring(_p)]=_c end end
function serverStart(_p)
  for k,v in pairs(file.list()) do p,n=string.match(k,"^(.*)__(.*)$");if p then file.rename(k,p.."/"..n) end p,n=nil,nil end
  local p=_p and tonumber(_p) or 80
  serverStatic=tostring(serverStatic)
  print("Starting Web Server - port: ",p)
  if serverService~=0 then serverService:close()end
  serverService=net.createServer(net.TCP,30)
  serverService:listen(p,function(_c) _c:on("receive",function(_c,_r)
    node.setcpufreq(node.CPU160MHZ)
    local su,err=pcall(function()
      local methodEnum,method,uri,proto,filename,path,dataType,data=httpd.request(_r)
      _r=nil
      --print (methodEnum,method,uri,proto,filename,path,dataType,data)
      if filename then
        if file.open(filename..".gz") then _c:send(httpd.response(200,httpd.file.mime(filename),"Content-Encoding: gzip\r\n",file.read()))
        elseif file.open(filename) then _c:send(httpd.response(200,httpd.file.mime(filename),"",file.read()))
        else _c:send(httpd.response(404))end
        file.close()
      else
        if not methodEnum then _c:send(httpd.response(400))
        elseif method == 0 then _c:send(httpd.response(501))
        else
          local cb = serverRouting[method] and serverRouting[method][path]
          if cb then
            if dataType=="json" and cjson then data=cjson.decode(data)end
            local cbs,ct,cont=cb(dataType,data)
            if cbs then
              if ct then _c:send(httpd.response(cbs,ct,"",cont))
              else _c:send(httpd.response(cbs))end
            else _c:send(httpd.response(204))end
            cb,cbs,ct,cont=nil
          else _c:send(httpd.response(404))end
        end
      end
      methodEnum,method,uri,proto,filename,path,dataType,data=nil
      node.setcpufreq(node.CPU80MHZ)
    end)
    if not su then _c:send(httpd.response(500,"text/plain","",err))end
    err=nil
  end) 
  _c:on("sent", function(sck) sck:close() end)
  end)
  return serverService
end
