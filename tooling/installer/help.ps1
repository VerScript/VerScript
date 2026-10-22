param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$TopicArgs
)
$topic = if ($TopicArgs) { $TopicArgs -join " " } else { "" }
& "$PSScriptRoot\verscript.exe" help $topic
