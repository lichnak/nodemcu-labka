pin=1
gpio.mode(pin,gpio.OUTPUT,gpio.PULLUP)
gpio.serout(pin,1,{30,30,60,60,30,30})  -- serial one byte, b10110010
gpio.serout(pin,1,{30,70},8)  -- serial 30% pwm 10k, lasts 8 cycles
gpio.serout(pin,1,{3,7},8)  -- serial 30% pwm 100k, lasts 8 cycles
gpio.serout(pin,1,{0,0},8)  -- serial 50% pwm as fast as possible, lasts 8 cycles
gpio.serout(pin,0,{20,10,10,20,10,10,10,100}) -- sim uart one byte 0x5A at about 100kbps
gpio.serout(pin,1,{8,18},8) -- serial 30% pwm 38k, lasts 8 cycles

gpio.serout(pin,1,{5000,995000},100, function() print("done") end) -- asynchronous 100 flashes 5 ms long every second with a callback function when done
gpio.serout(pin,1,{5000,995000},100, 1) -- asynchronous 100 flashes 5 ms long, no callback