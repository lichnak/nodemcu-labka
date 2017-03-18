require('ds18b20')
t = require("ds18b20")
t.setup(4)
addrs = t.addrs()
if (addrs ~= nil) then
print("Total DS18B20 sensors: "..table.getn(addrs))
end

function readData()
print(t.readNumber())
end

tmr.alarm(0, 10000, 1, function() readData() end )
