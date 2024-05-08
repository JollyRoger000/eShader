$client = New-Object System.Net.WebClient
$client.Credentials =
    New-Object System.Net.NetworkCredential("cs49635", "T4UTNW@v5Wsg")
$client.UploadFile(
    "ftp://vh424.timeweb.ru//public_html/eShader/updates/eShader.bin", "C:\Users\user\Documents\my_dev\EasyShade\eShader\build\eShader.bin")