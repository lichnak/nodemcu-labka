-- RC522 RFID Reader
-- Recoded by Petr Vavrin / pb.pb@centrum.cz
-- Original By Ben Jackson / https://github.com/capella-ben/LUA_RC522

RC522={}
RC522.__index=RC522

function RC522.dev(op,address,value)--op(0=read,1=write)
 gpio.write(RC522.ss,gpio.LOW)
 local val,adr=0,bit.band(bit.lshift(address,1),0x7E)
 if op==1 then spi.send(1,adr,value)
 else spi.send(1,bit.bor(adr,0x80))
  val=spi.recv(1,1)
 end
 gpio.write(RC522.ss,gpio.HIGH)
 return string.byte(val)
end

function RC522.bitmask(op,address,mask)--op(0=clear,1=set)
 local c=RC522.dev(0,address)
 RC522.dev(1,address,(op==1) and bit.bor(c,mask) or bit.band(c,bit.bnot(mask)))
end

function RC522.write(data)
 local data_out={}
 local length,last_bits,i,n=0,0,0,0
 RC522.dev(1,0x01,0x00)
 RC522.dev(1,0x04,0x7F)
 RC522.bitmask(1,0x0A,0x80)
 for i,n in pairs(data)do RC522.dev(1,0x09,n) end
 RC522.dev(1,0x01,0x0C)
 RC522.bitmask(1,0x0D,0x80)
 i=25
 while true do
  n=RC522.dev(0,0x04)
  i=i-1
  if not((i~=0)and(bit.band(n,0x01)==0)and(bit.band(n,0x30)==0))then break end
  tmr.delay(1)
 end
 RC522.bitmask(0,0x0D,0x80)
 if i~=0 and bit.band(RC522.dev(0,0x06),0x1B)==0x00 then
  n=RC522.dev(0,0x0A)
  last_bits=bit.band(RC522.dev(0,0x0C),0x07)
  length=(last_bits~=0) and (n-1)*8+last_bits or n*8
  n=(n<=0)and 1 or((n>16)and 16)or n
  for i=1,n do data_out[i]=RC522.dev(0,0x09)end
  return false,data_out,length
 end
 return true,nil,nil
end

function RC522.request()
 RC522.dev(1,0x0D,0x07)
 local err,data,bits=RC522.write({0x26})
 if err or(bits~=0x10)then return false,nil end
 return true,data
end

function RC522.anticoll()
 RC522.dev(1,0x0D,0x00)
 local snc,s,i,v=0,""
 local err,data,bits=RC522.write({0x93,0x20})
 if not err then for i, v in ipairs(data)do snc=bit.bxor(snc,data[i])end end
 if type(data)=="table" and table.maxn(data)==5 and snc==0 then for i,v in pairs(data)do s=s..string.format("%02X",v)end
 else err=true end
 return err,s
end

function RC522.init(rst,ss,cb)
 RC522.ss=ss
 spi.setup(1,spi.MASTER,spi.CPOL_LOW,spi.CPHA_LOW,spi.DATABITS_8,0)
 gpio.mode(rst,gpio.OUTPUT)
 gpio.mode(ss,gpio.OUTPUT)
 gpio.write(rst,gpio.HIGH)
 gpio.write(ss,gpio.HIGH)
 local init={0x01,0x0F,0x2A,0x8D,0x2B,0x3E,0x2D,30,0x2C,0,0x15,0x40,0x11,0x3D}
 for i=1,#init,2 do RC522.dev(1,init[i],init[i+1])end
 init=nil
 local c=RC522.dev(0,0x14)
 if bit.bnot(bit.band(c,0x03))then RC522.bitmask(1,0x14,0x03)end
 c=nil
 RC522.firmware=RC522.dev(0,0x37)
 RC522.initGain=RC522.dev(0,0x26)
 RC522.bitmask(0,0x26,bit.lshift(0x07,4))
 RC522.bitmask(1,0x26,bit.lshift(0x07,4))
 RC522.actGain=RC522.dev(0,0x26)
end

function RC522.start(cb)
 tmr.alarm(6,300,tmr.ALARM_AUTO,function()
  isTagNear,cardType=RC522.request()
  if isTagNear==true then
   tmr.stop(6)
   err,serialNo=RC522.anticoll()
   if type(cb)=="function" and err==false then cb(serialNo) end
   tmr.start(6)
  end
 end)
end

function RC522.stop() tmr.stop(6) end 


RC522.init(3,4) -- 3 Enable/reset pin, 4 SS (marked as SDA) pin  

print("RC522 Firmware Version: 0x"..string.format("%X",RC522.firmware))
print("Init gain",RC522.initGain)
print("Actual gain",RC522.actGain)

RC522.start( function(sn)
  print("Tag Found: "..sn)
end)
