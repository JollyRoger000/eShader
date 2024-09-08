$file = "build\eShader.bin";
$ftp = "ftp://vh424.timeweb.ru//public_html/eShader/updates/eShader.bin";
# $client = New-Object System.Net.WebClient
# $client.Credentials =
#     New-Object System.Net.NetworkCredential("cs49635", "T4UTNW@v5Wsg")
# $client.UploadFile($ftp, $file)


$request = [Net.WebRequest]::Create($ftp)
$request.Credentials =
    New-Object System.Net.NetworkCredential("cs49635", "T4UTNW@v5Wsg")
$request.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile 

$fileStream = [System.IO.File]::OpenRead($file)
$ftpStream = $request.GetRequestStream()

$buffer = New-Object Byte[] 10240
while (($read = $fileStream.Read($buffer, 0, $buffer.Length)) -gt 0)
{
    $ftpStream.Write($buffer, 0, $read)
    $pct = ($fileStream.Position / $fileStream.Length)
    Write-Progress `
        -Activity "Uploading" -Status ("{0:P0} complete:" -f $pct) `
        -PercentComplete ($pct * 100)
}

$ftpStream.Dispose()
$fileStream.Dispose()

Read-Host "Upload finished - press [ENTER] to exit"