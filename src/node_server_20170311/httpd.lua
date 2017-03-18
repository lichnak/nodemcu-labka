
require ( "ext_httpd" )

function setPinState(pin,value)
  local gpio=gpio
  gpio.mode(pin,gpio.OUTPUT)
  gpio.write(pin,value==1 and gpio.HIGH or gpio.LOW)
  return gpio.read(pin)
end

function getPinState(pin)
  local gpio=gpio
  return gpio.read(pin)
end

function switchPinState(pin)
  local gpio=gpio
  return setPinState(pin,gpio.read(pin)==1 and 0 or 1)
end

function saveToFile(_f,_c)
  local file,s=file,false
  if file.open(_f,"w")then
    file.write(_c)
    file.close()
    s=true
  end
  return s
end

serverOn ( "POST", "/api/wifi/set", function ( auth, dataType, data )
  local redirect = "/static/wifi.htm" 
  if data and data["ssid"] and data["password"] and data["mode"] then
    if data["mode"] == "AP" and saveToFile ( "ext_wifi_set.lua", 'wifiMode="AP"\n') == true then
      redirect = "/"
    end
    if data["mode"] == "STATION" and data["ssid"] ~= "" and saveToFile ( "ext_wifi_set.lua", 'wifiMode="STATION"\nwifiSSID="'..data["ssid"]..'"\nwifiPassword="'..(data["password"] or "")..'"\n') == true then
      redirect = "/"
    end
  end
  return 302, redirect
end)

local doorPin = 1
local doorState = setPinState ( doorPin, 0 )

serverOn ( "ALL", "/api/dash", function ( auth, dataType, data )
  local tempIn      = (math.random(5) + 20) .. "." .. math.random(9)
  local tempOut     = (math.random(5) + 20) .. "." .. math.random(9)
  doorState   = getPinState ( doorPin )
  local switchState = doorState == 1 and "true" or "false"
  local doorMessage = doorState == 1 and "OPENED" or "CLOSED"
  return "json", '{"#tint":"'..tempIn..'","#toutt":"'..tempOut..'","#tinv":{"_":"'..tempIn..'"},"#toutv":{"_":"'..tempOut..'"},"@doorSw":'..switchState..',"#doorMsg":"'..doorMessage..'"}'
end)

serverOn ( "POST", "/api/switch", function ( auth, dataType, data )
  
  
  if dataType == "form" and data and data.doorSw then
    doorState   = setPinState ( doorPin, ( data.doorSw == "true" and 1 or 0 ) )
    local switchState = doorState == 1 and "true" or "false"
    local doorMessage = doorState == 1 and "OPEN" or "CLOSE"
    return "json", '{"@doorSw":'..switchState..',"#doorMsg":"'..doorMessage..'"}'
  else
    return "json", '{}'
  end
end)

serverOn ( "POST", "/api/console", function ( auth, dataType, data )
  local node,wifi=node,wifi
  if dataType == "form" and data and data.cmd then
    if string.sub(data.cmd, 1, 1) == "=" then data.cmd = "return " .. string.sub(data.cmd, 2, -1) end
    local cmdf=loadstring(data.cmd)
    local ret=(cmdf()) or ""
    if type(ret)=="table" then
      local ser,k,v="","",""
      for k,v in pairs(ret)do if ser~="" then ser=ser..", " end ser=ser..tostring(k)..'="'..tostring(v)..'"' end
      return "text", ser
    else return "text", tostring(ret)
    end
  else return "text", 'NO CMD RECEIVED'
  end
end)

serverOn ( "ALL", "/api/gpio_stats", function ( auth, dataType, data )
  local read=gpio.read
  local i,out=0,""
  for i=0,12 do out=out..(out ~= "" and ',' or '')..'"#i'..i..'":"'..(read(i)==1 and 'ON' or 'OFF')..'"' end 
  return "json", '{'..out..'}'
end)

serverOn ( "ALL", "/test", function ( auth, dataType, data )
  --print ( dataType );
  if type(data)== "table" then
   --for k,v in pairs(data)do print ( "|"..k.."|","|"..v.."|") end
  end
  return "text", 'ok'
end)

urlEncode = function (str) return string.gsub (str, "([^%w ])", function (c) return string.format ("%%%02X", string.byte(c)) end) end

--serverOn ( "POST", "/api/file/open", function ( auth, dataType, data )
--    if data and data.filename then
--        print ( data.filename )
--        if file.open ( data.filename ) then
--            file.seek("end")
--            local length = file.seek()
--            file.seek("set",0)
--            local readLength = length > 1024 and 1024 or length
--            local content = file.read ( readLength )

--            content = urlEncode(content)
           -- return "json", '{filename:"'..data.filename..'",size:"'..length..'",readed
            
--        else
--            return "json", '{error:"file not found"}'
--        end
--    end
--    return "json", '{error:"no filename set"}'
--end)




