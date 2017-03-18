serverService=0
serverRouting={GET={},POST={}}
function serverOn(_m,_p,_c)local m=string.upper(tostring(_m));if serverRouting[m]then serverRouting[m][tostring(_p)]=_c end end
function serverStart(_p)
  for k,v in pairs(file.list()) do p,n=string.match(k,"^(.*)__(.*)$");if p then file.rename(k,p.."/"..n) end p,n=nil,nil end
  local p=_p and tonumber(_p) or 80
  print("Starting Web Server - port: ",p)
  if serverService~=0 then serverService:close()end
  serverService=httpd.createServer(p, "", function(_c,_method,_path,_dataType,_data)
    node.setcpufreq(node.CPU160MHZ)
    --print ( _method, _path, _dataType, _data )
    local cb = serverRouting[_method] and serverRouting[_method][_path]
    if cb then
      if dataType=="json" and cjson then data=cjson.decode(_data)end
      local ct,cont=cb(_dataType,_data)
      if ct==302 then _c:sendRedirect(tostring(cont))
      elseif cont then _c:sendResponse(ct,tostring(cont))
      else _c:sendStatus(tonumber(ct))end
      cb,ct,cont=nil
    else _c:sendStatus(404)end
    node.setcpufreq(node.CPU80MHZ)
  end) 
  return serverService
end

server = serverStart ( ) 
