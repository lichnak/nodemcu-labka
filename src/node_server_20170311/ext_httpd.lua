// This Source Code Form is subject to the terms of Creative Commons
// Attribution-ShareAlike 4.0 International License
// https://creativecommons.org/licenses/by-sa/4.0/
// Httpd Initialization
// Author: peterbay (Petr Vavrin) pb.pb(at)centrum.cz
serverRouting={GET={},POST={},ALL={}}
function serverOn(_m,_p,_c)local m=string.upper(tostring(_m));if serverRouting[m]then serverRouting[m][tostring(_p)]=_c end end
function serverStart(_p)
  for k,v in pairs(file.list()) do p,n=string.match(k,"^(.*)__(.*)$");if p then file.rename(k,p.."/"..n) end p,n=nil end
  local p=_p and tonumber(_p) or 80
  print("Starting Web Server - port: ",p)
  --httpd.createServer(p, "admin:admin", function(_c,_method,_path,_dataType,_data)
  httpd.createServer(p, "", "admin:admin", function(_c,_auth,_method,_path,_dataType,_data)
    node.setcpufreq(node.CPU160MHZ)
    local serverRouting = serverRouting
    --print ( _auth, _method, _path, _dataType, _data )
    --for k,v in pairs ( _data ) do print ( k, v ) end
    local cb = (serverRouting[_method] and serverRouting[_method][_path]) or (serverRouting.ALL and serverRouting.ALL[_path]) 
    if cb then
      if dataType=="json" and cjson then _data=cjson.decode(_data)end
      local ct,cont=cb(_auth,_dataType,_data)
      --print ( ct, cont )
      if ct==302 then _c:sendRedirect(tostring(cont))
      elseif cont then _c:sendResponse(ct,tostring(cont))
      else _c:sendStatus(tonumber(ct))end
      cb,ct,cont=nil
    else _c:sendStatus(404) end
    _data=nil
    collectgarbage()
    node.setcpufreq(node.CPU80MHZ)
  end) 
end

serverStart ( ) 
