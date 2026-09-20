Misc:
- Add a setting which is persisted and can be set in the gui which allows the swapping of the rotation direction of the rotary input. the default is the reverse to what it is now.

7 seg gui:
- the decimals are not relevant on the 7 segment gui. we would rather have it correctly display temperatures over 100. which are needed for doing steaming for example.
- in the gui we have the following settings in order:
1. current temp
2. set temp
3. presets
4. set time pre infuse
5. set time bloom
6. set time preheat
7. set time boost
8. power saving modes
9. ip

Presets:
- the initial state is there is just one preset called the "default" preset.
- chaning settings while operating the machine does not save them
- there is a ui button in the web gui which allows "save current settings as preset"
- there is a button to update the current preset
- saved presets have a name
- presets can be deleted
- presets can be reordered (maybe with drag and drop if that is possible or just buttons to move them up or down)


energy saving:
- the eco mode can be manually enabled and is a non alterable preset next to the default preset
- the eco mode is automaically turned on after N seconds where N is settable in the gui
- the sleep mode is atuomatically turned on after M seconds where M is settable in the gui
- M needs to be larger or equal than N

timer behaviour when in coffee mode:
- instead of the timer counting total seconds from 0 until coffee mode is exited we do:
    - count down how many seconds the current step in the coffee processes takes.
    - for example it starts with preheat and counts down from how many seconds preheat is set to, then it goes to pre infuse which will also count down, then for bloom, then for preheat, then for boost. then after boost is completed and we are in normal pid mode we display the total number of seconds the pump was already on, so the boost + normal pid time until we stop the coffee.

pid:
- we need to adjust, but i will do this manually.
- it needs to be more agressive and less accurate to ensure we dont have to wait as long.




