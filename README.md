# btfree
Bluetooth plugin for the PSP Go. Created by JJS.

Excerpt from the original release post (https://wololo.net/talk/viewtopic.php?f=5&t=1139) :

<blockquote>
I was trying to get more functionality out of the Bluetooth function on the GO. A problem is that the device registration rejects devices based on their class. This means a PC will not show up in the device list, but e.g. a cell phone does. So I wrote a small plugin that patches the Bluetooth VSH module in memory to remove this early device rejection. This is only effective for the pairing phase, so the plugin can be disabled after a device was registered and it will still remain working.
</blockquote>
