# Physical desktop mirror only. A1e gives the engine a separate logical
# client/render size; do not use this fit as the game render resolution.
function Get-AsylumWindowRenderSize([int]$EyeWidth,[int]$EyeHeight,[int]$WorkWidth,[int]$WorkHeight) {
    if($EyeWidth -lt 512 -or $EyeHeight -lt 512 -or $EyeWidth -gt 8192 -or $EyeHeight -gt 8192) {throw 'Headset render recommendation is out of bounds.'}
    # Room for non-client borders/titlebar and taskbar, independent of placement.
    $availableWidth=$WorkWidth-64;$availableHeight=$WorkHeight-64
    if($availableWidth -lt 512 -or $availableHeight -lt 512) {throw 'Desktop work area is too small for the Asylum VR window.'}
    $scale=[Math]::Min(1.0,[Math]::Min($availableWidth/[double]$EyeWidth,$availableHeight/[double]$EyeHeight))
    $width=[int]([Math]::Floor($EyeWidth*$scale/2)*2)
    $height=[int]([Math]::Floor($EyeHeight*$scale/2)*2)
    if($width -lt 512 -or $height -lt 512) {throw 'Desktop cannot fit the headset aspect ratio at the minimum render size.'}
    return @{width=$width;height=$height;scale=$scale}
}

function Get-AsylumVRRenderSize([int]$EyeWidth,[int]$EyeHeight,[double]$Scale=1.0) {
    if($EyeWidth -lt 512 -or $EyeHeight -lt 512 -or $EyeWidth -gt 8192 -or $EyeHeight -gt 8192) {throw 'Headset render recommendation is out of bounds.'}
    if([double]::IsNaN($Scale) -or [double]::IsInfinity($Scale) -or $Scale -lt .6 -or $Scale -gt 1.8) {throw 'Resolution scale must be from 0.6 to 1.8.'}
    $width=[int]([Math]::Floor($EyeWidth*$Scale/2)*2)
    $height=[int]([Math]::Floor($EyeHeight*$Scale/2)*2)
    if($width -lt 512 -or $height -lt 512 -or $width -gt 8192 -or $height -gt 8192) {throw 'Scaled resolution exceeds the supported range.'}
    return @{width=$width;height=$height;scale=$Scale}
}

# Mode-sized engine input. The centred compatibility lens encloses more than
# the actual eye; compensate its crop rather than reducing the visible detail.
function Get-AsylumDLSSRenderSize([int]$EyeWidth,[int]$EyeHeight,[double]$Scale,[int]$Mode,[double]$LensCrop=1.0) {
    $output=Get-AsylumVRRenderSize $EyeWidth $EyeHeight $Scale
    if($Mode -lt 0 -or $Mode -gt 5 -or [double]::IsNaN($LensCrop) -or [double]::IsInfinity($LensCrop) -or $LensCrop -lt .4 -or $LensCrop -gt 1) {throw 'Invalid DLSS mode or lens crop.'}
    $fraction=@(1.0,1.0,.77,(2.0/3.0),.58,.5)[$Mode]
    $factor=$fraction/$LensCrop
    $w=[int]([Math]::Floor(($output.width*$factor+1e-8)/2)*2)
    $h=[int]([Math]::Floor(($output.height*$factor+1e-8)/2)*2)
    if($w -lt 512 -or $h -lt 512 -or $w -gt 8192 -or $h -gt 8192) {throw 'DLSS scene input is out of bounds.'}
    return @{width=$w;height=$h;outputWidth=$output.width;outputHeight=$output.height;fraction=$fraction}
}
