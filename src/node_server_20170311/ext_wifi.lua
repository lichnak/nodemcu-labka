-- This Source Code Form is subject to the terms of Creative Commons
-- Attribution-ShareAlike 4.0 International License
-- https://creativecommons.org/licenses/by-sa/4.0/
-- Wifi Initialization
-- Author: peterbay (Petr Vavrin) pb.pb(at)centrum.cz
wifiSelectPin = 12
wifiApPassword = ""
wifiMode = ""

function wifiInit ()
  gpio.mode ( wifiSelectPin, gpio.INPUT )
  wifiMode = gpio.read ( wifiSelectPin ) == 1 and "STATION" or "SOFTAP"
  print ('\nWIFI initialization - mode('..wifiMode..')\n')
  if wifiMode=="STATION" then
    print ("STATION")
    wifi.setmode(wifi.STATION)
--    wifi.sta.getap(1,wifiConnect)
    dofile("ext_wifi_settings.lua")
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

  elseif wifiMode == "SOFTAP" then
    wifi.setmode(wifi.SOFTAP)

    wifi.ap.config({
      ssid="NODE_"..string.gsub(tostring(wifi.ap.getmac()),":",""),
      pwd=wifiApPassword and wifiApPassword ~= "" and wifiApPassword or "node1234"
    })

    wifi.ap.setip({ip = "10.0.0.1", netmask="255.255.255.0", gateway="10.0.0.1" })
  end
end
