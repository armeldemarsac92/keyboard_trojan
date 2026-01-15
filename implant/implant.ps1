$s=@'
[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")]public static extern int GetWindowText(IntPtr h,System.Text.StringBuilder s,int n);
[DllImport("kernel32.dll")]public static extern IntPtr CreateFile(string n,uint a,uint s,IntPtr p,uint d,uint f,IntPtr h);
[DllImport("hid.dll")]public static extern bool HidD_SetFeature(IntPtr h,byte[] b,int l);
'@
$a=Add-Type -M $s -N W32 -PassThru
# On cible l'interface MI_05 du Teensy
$dev=Get-PnpDevice|?{$_.InstanceId -match "VID_16C0&PID_0487&MI_05"}
$p=(Get-PnpDeviceProperty -InstanceId $dev[0].InstanceId -KeyName "DEVPKEY_Device_PDOName").Data
$h=$a::CreateFile($p,0xC0000000,3,[IntPtr]::Zero,3,0,[IntPtr]::Zero)
$l=""
while(1){
    $w=$a::GetForegroundWindow()
    $b=New-Object System.Text.StringBuilder 256
    $a::GetWindowText($w,$b,256)
    $t=$b.ToString()
    if($t -ne $l -and $t -ne ""){
        $l=$t
        $f=New-Object byte[] 65
        $f[0]=4 # Report ID
        $e=[System.Text.Encoding]::ASCII.GetBytes($t)
        [Array]::Copy($e,0,$f,1,[Math]::Min($e.Length,64))
        $a::HidD_SetFeature($h,$f,65)
    }
    Sleep -m 500
}




