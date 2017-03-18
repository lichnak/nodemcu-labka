
require ( "ext_httpd" )

function setPinState ( pin, value )
  gpio.mode(pin,gpio.OUTPUT)
  if value==1 then gpio.write(pin,gpio.HIGH)
  else gpio.write(pin,gpio.LOW)
  end
  return gpio.read(pin)
end

function getPinState(pin)
  return gpio.read(pin)
end

function switchPinState(pin)
  if gpio.read(pin)==1 then
    return setPinState(pin,0)
  else
    return setPinState(pin,1)
  end
end

function saveToFile(_f,_c)
  local s=false
  if file.open(_f,"w")then
    file.write(_c)
    file.close()
    s=true
  end
  return s
end

serverOn ( "POST", "/api/wifi/set", function ( dataType, data )
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

serverOn ( "POST", "/api/dash", function ( dataType, data )
  local tempIn      = (math.random(5) + 20) .. "." .. math.random(9)
  local tempOut     = (math.random(5) + 20) .. "." .. math.random(9)
  doorState   = getPinState ( doorPin )
  local switchState = doorState == 1 and "true" or "false"
  local doorMessage = doorState == 1 and "OPENED" or "CLOSED"
  return "json", '{"#tint":"'..tempIn..'","#toutt":"'..tempOut..'","#tinv":{"_":"'..tempIn..'"},"#toutv":{"_":"'..tempOut..'"},"@doorSw":'..switchState..',"#doorMsg":"'..doorMessage..'"}'
end)

serverOn ( "POST", "/api/switch", function ( dataType, data )
  if dataType == "form" and data and data.doorSw then
    doorState   = setPinState ( doorPin, ( data.doorSw == "true" and 1 or 0 ) )
    local switchState = doorState == 1 and "true" or "false"
    local doorMessage = doorState == 1 and "OPENED" or "CLOSED"
    return "json", '{"@doorSw":'..switchState..',"#doorMsg":"'..doorMessage..'"}'
  else
    return "json", '{}'
  end
end)

serverOn ( "POST", "/api/console", function ( dataType, data )
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

serverOn ( "POST", "/api/gpio_stats", function ( dataType, data )
  local i,out=0,""
  for i=0,12 do out=out..(out ~= "" and ',' or '')..'"#i'..i..'":"'..(gpio.read(i)==1 and 'ON' or 'OFF')..'"' end 
  return "json", '{'..out..'}'
end)
