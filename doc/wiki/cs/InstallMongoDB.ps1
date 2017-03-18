#Set MongoDB Installation variables
$MONGODBWEBFILE = "https://fastdl.mongodb.org/win32/mongodb-win32-x86_64-2008plus-ssl-3.2.10-signed.msi"
$MONGODBINSTALLLOCATION = "C:\Program Files\mongodb"
$MONGODBSOURCE = "C:\downloads"
$MONGODBMSIFILE = $MONGODBSOURCE + "\mongodb-win32-x86_64-2008plus-ssl-3.2.10-signed.msi"
$MONGODBBINPATH = $MONGODBINSTALLLOCATION + "\Server\3.2\bin"
$MONGODBMONGOD = $MONGODBBINPATH + "\mongod.exe"
$MONGODBDATAPATH = "C:\Data"
$MONGODBDATABASEFOLDER = $MONGODBDATAPATH + "\db"
$MONGODBLOGFOLDER = $MONGODBDATAPATH + "\log"
$MONGODBLOGFILE = $MONGODBLOGFOLDER + "\mongod.log"
$MONGODBCONFFILE = $MONGODBBINPATH + "\mongod.cfg"
$MONGODBCONFSOURCE = $MONGODBSOURCE + "\mongod.cfg"
$MONGODBCONF = "systemLog:`n    destination: file`n    path: " + $MONGODBLOGFILE + "`nstorage:`n    dbPath: " + $MONGODBDATABASEFOLDER
$MONGODBSERVICENAME = "MongoDB"

#Download MSI package
$WebClient = New-Object System.Net.WebClient
$WebClient.DownloadFile($MONGODBWEBFILE, $MONGODBMSIFILE)

#Install MongoDB MSI package
Invoke-Expression "& msiexec.exe /q /i `"$MONGODBMSIFILEPATH`" INSTALLLOCATION=`"$INSTALLLOCATION`" ADDLOCAL=all"

#Waiting 30s for msiexec installs MongoDB package
Start-Sleep 30

#Add MongoDB binary folder to PATH variable
$env:path += ";" + $MONGODBBINPATH
[Environment]::SetEnvironmentVariable("PATH", $env:path, "Machine")

#Create database and log folders
If (-Not (Test-Path -Path $MONGODBDATAPATH))
{
New-Item -ItemType "directory" -Path $MONGODBDATAPATH -Force
}
If (-Not (Test-Path -Path $MONGODBDATABASEFOLDER))
{
New-Item -ItemType "directory" -Path $MONGODBDATABASEFOLDER -Force
}
If (-Not (Test-Path -Path $MONGODBLOGFOLDER))
{
New-Item -ItemType "directory" -Path $MONGODBLOGFOLDER -Force
}

#Create log file
If (-Not (Test-Path -Path $MONGODBLOGFILE))
{
New-Item -ItemType "file" -Path $MONGODBLOGFILE -Force
}

#Push MongoDB configuration
If (Test-Path -Path $MONGODBCONFSOURCE)
{
Copy-Item $MONGODBCONFSOURCE -Destination $MONGODBBINPATH -Force
}
ElseIf (Test-Path -Path $MONGODBCONFFILE)
{
Write-Host "INFO: MongoDB Configuration file exists at " $MONGODBCONFFILE
}
Else
{
New-Item -ItemType "file" -Path $MONGODBCONFFILE -Force
$MONGODBCONF | Out-File -FilePath $MONGODBCONFFILE -Append
}

#Configure ans start Windows Service for MongoDB
#Invoke-Expression "& `"$MONGODBMONGOD`" --remove"
If (Get-Service $MONGODBSERVICENAME -ErrorAction SilentlyContinue)
{
Write-Host "INFO: MongoDB Service exists"
}
Else
{
Invoke-Expression "& `"$MONGODBMONGOD`" --config `"$MONGODBCONFFILE`" --install"
Set-Service $MONGODBSERVICENAME -Status Running
}