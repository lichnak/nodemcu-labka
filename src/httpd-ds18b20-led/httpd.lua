-- httpd.lua --
print('\nLoading httpd.lua ...\n')

local ssid = "WIFINET"
local pass = "YOUR_PASSWORD"

-- Configure Wireless Internet
print('\nLichnak wifi.lua\n')
wifi.setmode(wifi.STATION)
print('set mode=STATION (mode='..wifi.getmode()..')\n')
print('MAC Address: ',wifi.sta.getmac())
print('Chip ID: ',node.chipid())
print('Heap Size: ',node.heap(),'\n')
-- Wifi Config Start
wifi.sta.config(ssid,pass)
-- Wifi Config End

-- WiFi Connection Verification
tmr.alarm(0, 1000, 1, function()
    if wifi.sta.getip() == nil then
        print("Connecting to AP...\n")
    else
        ip, nm, gw=wifi.sta.getip()
        print("IP Info: \nIP Address: ",ip)
        print("Netmask: ",nm)
        print("Gateway Addr: ",gw,'\n')
        tmr.stop(0)
    end
end)

-- Read DS18B20 with index 1
function getTemperature(pin, unit)
    local th = nil
    th = require("ds18b20")
    th.setup(pin)
    local addrs = th.addrs()
    if (addrs == nil) then
        print("None DS18B20 sensors detected: "..table.getn(addrs))
    end
    local t = th.readNumber(addrs[1], unit)
    if (t == nil) then
      return nil
    else
      return t
    end
    --Unload DS18B20
    th = nil
    ds18b20 = nil
    package.loaded["ds18b20"]=nil
end

-- Web Server --
-- Build and return a table of the http request data
function getHttpReq(payload)
   local t = {}
   local first = nil
   local key, v, strt_ndx, end_ndx

   for str in string.gmatch (payload, "([^\n]+)") do
      -- First line in the method and path
      if (first == nil) then
         first = 1
         strt_ndx, end_ndx = string.find (str, "([^ ]+)")
         v = trim (string.sub (str, end_ndx + 2))
         key = trim (string.sub (str, strt_ndx, end_ndx))
         t["METHOD"] = key
         t["REQUEST"] = v
      else -- Process and reamaining ":" fields
         strt_ndx, end_ndx = string.find (str, "([^:]+)")
         if (end_ndx ~= nil) then
            v = trim (string.sub (str, end_ndx + 2))
            key = trim (string.sub (str, strt_ndx, end_ndx))
            t[key] = v
         end
      end
   end

   return t
end

-- String trim left and right
function trim(s)
  return (s:gsub ("^%s*(.-)%s*$", "%1"))
end

-- On Receive function
function receive(conn, payload)
    -- ESP Update 
    function espUpdate(postparse,pin)
        local mcu_post=string.sub(payload,postparse[2]+1,#payload)
        local t = nil
        -- Identify LED Switch button
        if mcu_post == "LED+Switch" then 
            gpio.mode(pin, gpio.OUTPUT)
            if gpio.read(pin)==1 then
                gpio.write(pin, gpio.LOW)
                print("LED was turned off: " .. gpio.read(pin))
                t = gpio.read(pin)
            else
                gpio.write(pin,gpio.HIGH)
                print("LED was turned on: " .. gpio.read(pin))
                t = gpio.read(pin)
            end
        end
        return t
    end
    -- parse payload request data
    -- print(payload)
    local queryData = getHttpReq(payload)
    local getPeerIP = conn:getpeer()
    local ledPin = 1
    print("IP Address: " .. getPeerIP)
    print("HTTP Request method: " .. queryData["METHOD"])
    print("HTTP User Agent: " .. queryData["User-Agent"] .. "\n")
    --parse POST value from header
    local postParse = nil
    local ledState = gpio.read(ledPin)
    if queryData["METHOD"]=="POST" then local
        postParse={string.find(payload,"mcu_post=")}
        --If POST value name mcu_post exists then set LED power
        if postParse[2]~=nil then
           ledState = espUpdate(postParse,ledPin)
        end
    end
    -- HTML Content
    local contentHtml='<html><head></head><body><h1>Temperature: '..getTemperature(4,'C')..'&#8451</p></h1>'..ledState..'<br><form action="" method="POST"><input type="submit" name="mcu_post" value="LED Switch"></body></html>'
    --local contentHtml='<!DOCTYPE html><html><head></head><body><h1>Temperature: '..getTemperature(4,'C')..'&#8451</p></h1><br><form action="" method="POST"><input type="submit" name="mcu_post" value="LED Switch"></body></html>'
    -- HTTP Header
    local contentLength=string.len(contentHtml)
    local contentHeader="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\Content-Length:" .. contentLength .. "\r\n\r\n"
    print(contentHeader .. contentHtml .. "\n\n")
    conn:send(contentHeader .. contentHtml)
    conn:on("sent", function(sck) sck:close() end)
end
-- Connection function
function connection(conn) 
    conn:on("receive", receive)
end

print("Starting Web Server...")
-- Create a server object with 30 second timeout
srv=net.createServer(net.TCP, 30) 
srv:listen(8080, connection)
