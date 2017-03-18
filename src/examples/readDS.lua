t=require("ds18b20")
print("temp is " ..t.readNumber(4))
t=nil
ds18b20=nil
package.loaded["ds18b20"]=nil