[CmdletBinding()]
param(
    [Parameter()]
    [string]$scadPath,

	[Parameter()]
    [ValidateSet("CSV", "JSON", "SCAD")]
	[string]$inputType,

	[Parameter()]
	[string]$csvPath,

    [Parameter()]
    [string]$outputFolder,

    [Parameter()]
    [ValidateSet("STL", "OFF", "AMF", "3MF", "DXF", "SVG", "PNG", "CSV", "JSON")]
    [string]$fileExtension,

    [Parameter()]
    [string]$camArgs,

    [Parameter()]
    [bool]$overwriteFiles=$false,
    
    [Parameter()]
    [int]$processCount
)

############################################

function pause{ $null = Read-Host 'Press Enter to continue...' }

function promptForFile($name, $filter, $OSType){
    if($OSType){
        Clear-Host
        Write-Host "You will now be prompted for the $name"
        pause

        $FileBrowser.filter = "$filter"
        [void]$FileBrowser.ShowDialog()

        $FileBrowser.FileName
    } else {
        Clear-Host
        Read-Host -Prompt "Input the full .$name file path"
    }
}

function testPathAndPrompt($item, $name, $filter, $OSType){
    while(!(Test-Path $item)){
        $item = promptForFile -name $name -filter $filter -OSType $IsWin
    }
    $item
}

function promptIfNotSet($item, $name, $filter, $OSType){
    do {
        $item = promptForFile -name $name -filter $filter -OSType $IsWin
    } while(!(Test-Path $item))
    $item
}

function fileCheck($prompt, $item, $name, $filter, $OSType){
    if($prompt){
        $item = promptIfNotSet -item $item -name $name -filter $filter -OSType $IsWin
    }
    testPathAndPrompt -item $item -name '.SCAD file' -filter 'scad (*.scad)| *.scad' -OSType $IsWin
}

function jsonExport($csv, $scad){
    
    $JsonPath = (Get-ItemProperty $scad).DirectoryName + '\' + [System.IO.Path]::GetFileNameWithoutExtension($scad) + ".json"

    if(Test-Path -Path $JsonPath){
        Write-Host ".JSON file already exists, creating backup..."
        Copy-Item -Path $JsonPath -Destination ((Get-ItemProperty $scadPath).DirectoryName + '\' + (Get-Date -Format 'yyMMdd-hhmm') + '-' + [System.IO.Path]::GetFileNameWithoutExtension($scadPath) + ".json")
    }

    # Build the json structure
    $jsonBase = @{}
    $parameterSets = @{}

    # Import the CSV
    $csvData = Import-Csv -Path $csv

    # For each item:
    $csvData | ForEach-Object {
    
        # Set the name for the current parameter set
        $parameterSetName = $_.exported_filename
    
        # Add the current set to the array
        $parameterSets.Add($parameterSetName,$_)
    }

    # Add the items to the json base
    $jsonBase.Add("parameterSets",$parameterSets)

    # Output to a file
    $jsonBase | ConvertTo-Json | Set-Content $JsonPath
}

function folderPrompt($OSType){
    if ($OSType) {
        Clear-Host
        Write-Host "You will now be prompted for the OUTPUT folder"
        pause

        [void]$FolderBrowser.ShowDialog()
        $output = $FolderBrowser.SelectedPath
    } else {
        # Linux
        Clear-Host
        $output = Read-Host -Prompt 'Input the export folder path'

        if ($output -notmatch '\/$'){
            # $output += '/'
        }
    }
    $output
}

function testFolderAndCreate($path){
    If(!(test-path $path)){
        New-Item -ItemType Directory -Force -Path $path
    }
}

function promptForFileExtension($OSType){
    if($OSType){
    
        $fileExtension_list = @(
            "STL", "OFF", "AMF", "3MF", "DXF", "SVG", "PNG", "CSV", "JSON"
        )

        $GridArguments = @{
            OutputMode = 'Single'
            Title      = 'Please select a export format and click OK'
        }

        $file_ext = $fileExtension_list | Sort-Object | Out-GridView @GridArguments | ForEach-Object {
            $_
        }

    } else {

        Clear-Host
        Write-Host "Enter one of the following options: STL, OFF, AMF, 3MF, DXF, SVG, PNG, CSV, JSON"
    
        $file_ext = Read-Host -Prompt 'Input the file extension'
    }

    $file_ext

}

function promptForInputType($OSType){
    if($OSType){
    
        $fileExtension_list = @(
            "CSV", "JSON", "SCAD"
        )

        $GridArguments = @{
            OutputMode = 'Single'
            Title      = 'Please select a input format and click OK'
        }

        $file_ext = $fileExtension_list | Sort-Object | Out-GridView @GridArguments | ForEach-Object {
            $_
        }

    } else {

        Clear-Host
        Write-Host "Enter one of the following options: CSV, JSON, SCAD"
    
        $file_ext = Read-Host -Prompt 'Input the file extension'
    }

    $file_ext

}

function overwriteFile($path){
    if(Test-Path $path){
        Remove-Item -Path $path -Force
    }
}

############################################

### Check if Windows or Linux
if($IsWindows -or ((Get-Host | Select-Object Version).Version.Major -lt 7) ){
    [bool]$IsWin = $true
    ### Add forms
    Add-Type -AssemblyName System.Windows.Forms
    $FileBrowser = New-Object System.Windows.Forms.OpenFileDialog
    $FolderBrowser = New-Object System.Windows.Forms.FolderBrowserDialog
    ### Check for OpenSCAD .exe in deafult location
    if(Test-Path -Path "C:\Program Files\OpenSCAD\openscad.exe"){
        $fileExe = "C:\Program Files\OpenSCAD\openscad.exe"
    } else {
        Clear-Host
        Write-Output "OpenSCAD .exe file not found in default location, you will need to select the .exe file manually"
        pause

        $FileBrowser.filter = "exe (*.exe)| *.exe"
        [void]$FileBrowser.ShowDialog()

        $fileExe = $FileBrowser.FileName
    }

} else {
    [bool]$IsWin = $false
    $fileExe = ''
}

### SCAD FILE
$scadPath = fileCheck -prompt (!$PSBoundParameters.ContainsKey('scadPath')) -item $scadPath -name '.SCAD file' -filter 'scad (*.scad)| *.scad' -OSType $IsWin

### Prompt for input if not set
if ( !$PSBoundParameters.ContainsKey('inputType') ){
    $inputType = promptForInputType -OSType $IsWin
}

### Prompt for output extension if not set
if ($inputType -ne "SCAD" -and (!$PSBoundParameters.ContainsKey('fileExtension'))) {
    $fileExtension = promptForFileExtension -OSType $IsWin
}

##############################
if ($fileExtension -eq "JSON" ){
    $csvPath = fileCheck -prompt (!$PSBoundParameters.ContainsKey('csvPath')) -item $csvPath -name '.CSV file' -filter 'csv (*.csv)| *.csv' -OSType $IsWin
    jsonExport -csv $csvPath -scad $scadPath
} elseif ($inputType -eq "SCAD") {
    
    ### DO SCAD TO CSV EXPORT
    $csvPath = fileCheck -prompt (!$PSBoundParameters.ContainsKey('csvPath')) -item $csvPath -name '.CSV file' -filter 'csv (*.csv)| *.csv' -OSType $IsWin

    ####################################################
    # Path for temporary json output file
    $tempJsonPath = (Get-ItemProperty $scadPath).DirectoryName + '\' + (Get-Date -Format 'yyMMdd-hhmm') + '-temp-' + [System.IO.Path]::GetFileNameWithoutExtension($scadPath) + ".json"
    # Set the arguments
    $arguments = '--export-format param -o "' + $tempJsonPath + '" "' + $scadPath + '"'
    # Run the command
    if($IsWindows){
        start-process $using:fileExe $arguments -Wait
    } else {
        start-process openscad $arguments -Wait
    }
    # Build table with name
    $myObject = [PSCustomObject]@{
        exported_filename     = 'default'
    }
    # Add each item to the table
    (Get-Content $tempJsonPath | ConvertFrom-Json).parameters | ForEach-Object {
        $myObject | Add-Member -MemberType NoteProperty -Name $_.name -Value $_.initial
    }
    # Export to CSV
    $myObject | export-csv -Path $CSVfile -NoTypeInformation
    # Cleanup JSON file
    Remove-Item -Path $tempJsonPath

    Clear-Host
    write-host "CSV file has been created:
    $CSVfile"

    pause
    ####################################################
    
} else {

    if($isWin){
        $JsonPath = (Get-ItemProperty $ScadPath).DirectoryName + '\' + [System.IO.Path]::GetFileNameWithoutExtension($ScadPath) + ".json"
    } else {
        $JsonPath = (Get-ItemProperty $ScadPath).DirectoryName + '/' + [System.IO.Path]::GetFileNameWithoutExtension($ScadPath) + ".json"
    }
    ### IF CSV, export the json file first
    if($inputType -eq "CSV"){
        $csvPath = fileCheck -prompt (!$PSBoundParameters.ContainsKey('csvPath')) -item $csvPath -name '.CSV file' -filter 'csv (*.csv)| *.csv' -OSType $IsWin
        jsonExport -csv $csvPath -scad $scadPath
    } else {
        if(!(Test-Path $JsonPath)){
            Write-Host "JSON FILE MISSING... Script will exit"
            pause
            exit
        }
    }

    ##########################################################################################################################################
    ## Check/set/Set output folder
    if ( !$PSBoundParameters.ContainsKey('OutputFolder') ){
        $outputFolder = folderPrompt -OSType $IsWin
    }
    testFolderAndCreate -path $outputFolder

    ##### CAMERA ARGUMENTS IF REQUIRED
    If ($fileExtension -eq "PNG" -and !$PSBoundParameters.ContainsKey('cam_args')){

Clear-Host
Write-Host "Default Camera Settings
Leave Blank

Specific Camera Location ( translate_x,y,z, rot_x,y,z, dist)
    --camera 0,0,0,120,0,50,140

Auto-center
    --autocenter

View All
    --viewall
 
Color Scheme
    --colorscheme DeepOcean

User Render Quality
    --render

Set Image Export Size
    --imgsize 100,100



Example - Top Down, View All, Auto-Center, Deep Ocean Color Scheme
    --camera 0,0,0,0,0,0,0 --viewall --autocenter --colorscheme DeepOcean

Example - Default camera position, View All, Auto-Center, Deep Ocean Color Scheme
    --viewall --autocenter --imgsize 1024,1024 --render --colorscheme DeepOcean
    
"

$camArgs = Read-Host -Prompt 'Input the camera arguments then press enter'
clear-host

}

    ### RUN THE EXPORT
    ##########################################################################################################################################

    if((Get-Host | Select-Object Version).Version.Major -gt 6){

        ### Set default process count if not set in args
        if ( !$PSBoundParameters.ContainsKey('process_count') ){
            [int]$processCount = 3
        }

        $importedJSON = Get-Content $jsonPath | convertfrom-json
        $totalItemCount = (Get-Member -MemberType NoteProperty -InputObject $importedJSON[0].parameterSets).count
        $dataset = @()

        $importedJSON[0].parameterSets | Get-Member -MemberType NoteProperty | ForEach-Object {
            $index += 1
            # Set the current file name to Parameter Set Name + .stl
            $Output_Filename = $_.Name + '.' + $fileExtension
            # Set the Output Path
 
            if($IsWin){
                if ($outputFolder -match '\\$'){
                    $OutputPath = $outputFolder + $Output_Filename
                } else {
                    $OutputPath = $outputFolder + '\' + $Output_Filename
                }
            } else {
                $OutputPath = $outputFolder + $Output_Filename
            }


            ### CHECK IF FILE EXISTS AND IF OVERWRITE IS SET
            
            if( $overwriteFiles -or -not(Test-Path -Path $OutputPath) ){

                # Set the arguments
                If ($fileExtension -eq "PNG"){
                    $arguments = '-o "' + $OutputPath + '" ' + $camArgs + ' -p "' + $jsonPath + '" -P "' + $_.Name + '" "' + $scadPath + '"'
                } else {
                    $arguments = '-o "' + $OutputPath + '" -p "' + $jsonPath + '" -P "' + $_.Name + '" "' + $scadPath + '"'
                }

                # Run the command
                $current_data_set = @{
                    Id=$index
                    Value=$arguments
                    Name=$_.Name
                }
                $dataset += $current_data_set
            }

        }

        # Create a hashtable for process.
        # Keys should be ID's of the processes
        $origin = @{}
        $dataset | Foreach-Object {$origin.($_.id) = @{}}

        # Create synced hashtable
        $sync = [System.Collections.Hashtable]::Synchronized($origin)

        $job = $dataset | Foreach-Object -ThrottleLimit $processCount -AsJob -Parallel {
            $syncCopy = $using:sync
            $process = $syncCopy.$($PSItem.Id)

            $process.Id = $PSItem.Id
            $process.Activity = "$($PSItem.Name).$using:fileExtension - $($PSItem.Id) of $using:totalItemCount"
            $process.Status = "Processing"

            #$PSItem.Value
            $arguments = $PSItem.Value
            if($IsWindows){
                start-process $using:fileExe $arguments -Wait -RedirectStandardError NUL
            } else {
                start-process openscad $arguments -Wait
            }

            # Mark process as completed
            $process.Completed = $true
        }

        # Show progress
        while($job.State -eq 'Running')
        {
            $sync.Keys | Foreach-Object {
                # If key is not defined, ignore
                if(![string]::IsNullOrEmpty($sync.$_.keys))
                {
                    # Create parameter hashtable to splat
                    $param = $sync.$_

                    # Execute Write-Progress
                    Write-Progress @param
                }
            }

            # Wait to refresh to not overload gui
            Start-Sleep -Seconds 0.1
        }

        # Write-Host "Done..."
        pause

    } else {

        ##### START STL EXPORT #####
        # Import JSON
        If(Test-Path -Path $JsonPath){

            $importedJSON = Get-Content $jsonPath | convertfrom-json

            # Count Items
            $totalItemCount = (Get-Member -MemberType NoteProperty -InputObject $importedJSON[0].parameterSets).count

            # For Each Item
            $current_count = 0
            $importedJSON[0].parameterSets | Get-Member -MemberType NoteProperty | ForEach-Object {
        
                # Increase the count by 1
                $current_count += 1

                # Set the current file name to Parameter Set Name + .stl
                $Output_Filename = $_.Name + '.' + $fileExtension
                # Set the Output Path
                if ($outputFolder -match '\\$'){
                    $OutputPath = $outputFolder + $Output_Filename
                } else {
                    $OutputPath = $outputFolder + '\' + $Output_Filename
                }

                ## Check if item exists, if not
                if ($overwriteFiles -or -not(Test-Path -Path $OutputPath)) {

                    # Set the arguments
                    If ($fileExtension -eq "PNG"){
                        $arguments = '-o "' + $OutputPath + '" ' + $camArgs + ' -p "' + $jsonPath + '" -P "' + $_.Name + '" "' + $scadPath + '"'
                    } else {
                        $arguments = '-o "' + $OutputPath + '" -p "' + $jsonPath + '" -P "' + $_.Name + '" "' + $scadPath + '"'
                    }

                    # Run the command
                    start-process $fileExe $arguments -Wait -RedirectStandardError NUL

                    $export_count += 1

                } else {
                    $skip_count += 1
                }

                # Write-Progress -Activity ("Exporting " + $Output_Filename) -Status ("Progress: " + $current_count + " of " + $totalItemCount) -PercentComplete ($current_count/$totalItemCount*100)

            }

            Clear-Host
            Write-Host "Exported: $export_count
            Skipped: $skip_count
            Total: $totalItemCount"

            pause

        } else {
        
            Clear-Host
            Write-host "Missing JSON file..."
            pause

        }

    }
}