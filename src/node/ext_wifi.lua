
wifiSelectPin = 12
wifiApPassword = ""
wifiMode = ""

function wifiConnect(_t)

    dofile("ext_wifi_settings.lua")

    if wifiSSID and wifiPassword and type(wifiSSID)=="string" and type(wifiPassword)=="string" then
        for bssid,v in pairs(_t) do
            local ssid,rssi,authmode,channel=string.match(v,"([^,]+),([^,]+),([^,]+),([^,]*)")
            ssid=string.match(ssid,"^%s*(.*)%s*$")
            if wifiSSID==ssid then
                wifi.sta.config(wifiSSID,wifiPassword)
                print("WIFI - Connecting to SSID ("..wifiSSID..")...")
                tmr.alarm ( 0, 1000, 1, function ()
                    if wifi.sta.getip() == nil then
                        --print ( "Connecting to AP...\n")
                    else
                        local ip,nm,gw = wifi.sta.getip()
                        print("IP Info: \nIP Address: ",ip)
                        print("Netmask: ",nm)
                        print("Gateway Addr: ",gw,'\n')
                        tmr.stop(0)
                    end
                end)
                do return end
            end
        end
    else
        print("ERROR: ext_wifi_settings.lua does not contain wifiSSID and/or wifiPassword")
        print ( wifiSSID )
        print (wifiPassword)
    end
end

function wifiInit ()

    gpio.mode(wifiSelectPin,gpio.INPUT)

    wifiMode=gpio.read(wifiSelectPin)==1 and "STATION" or "SOFTAP"

    print ('\nWIFI initialization - mode('..wifiMode..')\n')

    if wifiMode=="STATION" then
        wifi.setmode(wifi.STATION)
        wifi.sta.getap(1,wifiConnect)

    elseif wifiMode == "SOFTAP" then
        wifi.setmode(wifi.SOFTAP)

        wifi.ap.config({
            ssid="NODE_"..string.gsub(tostring(wifi.ap.getmac()),":",""),
            pwd=wifiApPassword and wifiApPassword ~= "" and wifiApPassword or "node1234"
        })

        wifi.ap.setip({
            ip = "10.0.0.1",
            netmask="255.255.255.0",
            gateway="10.0.0.1"
        })
        
--     print("Soft AP started")
--     print("Heep:(bytes)"..node.heap());
--     print("MAC:".. tostring(wifi.ap.getmac()).."\r\nIP:"..tostring(wifi.ap.getip()));


    end
end

