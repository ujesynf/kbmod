# kbmod

A mechanical keyboard configuration for written in C++ for MacOS M-series laptop devices only.

## What audio files does it rely on?

The program relies on audio directories from kbsim's Github page as the code inside aligns with the names of the audio files of the keys.

## How do I set this up?

- Download the source file
- Open a new terminal window inside of the main directory [named 'kbmod'] and run:

```sh
make
```

- Copy the executable file into `/usr/local/bin` via:

```sh
sudo cp kbmod /usr/local/bin
```

It is up to you to decide where to keep your sound profiles. According to my personal preference I like placing them inside `/usr/local/share/kbmod` which you will see later below. If that is the case with you as well, please make a `kbmod` directory at that path.

This program relies on a YAML configuration file if you want to change the default sound profile [which is set to `turquoise`]. If there is an available configuration file, the program looks for it in `~/.kbmod/config.yaml`. The configuration file looks for the following fields:

```yaml
key_pitch: 0.00
key_pitchvar: 0.5
key_volume: 0.4
key_sound_library: /usr/local/share/kbmod/mxblack
key_fallback_sound_library: /usr/local/share/kbmod/turquoise
```

- 'pitch' is a float here because keys are detuned down to the cent level (where +1 cent or 0.01 in pitch is a hundredth of a semitone up). The acceptable range of values for `key_pitch` is -24.00 to +24.00.
- 'pitchvar' means pitch variation here. The program randomizes key pitch between -0.5 and +0.5 if your `key_pitchvar` is set to `0.5`. The acceptable range of values for `key_pitchvar` is 0.0 to 5.0.
- 'volume' is self-explanatory. Its acceptable range of values is between 0.0 and 1.0.
- 'sound_library' refers to the path of where the selected sound profile is situated. If you are writing your own configuration file, make sure that the path of your sound profile is not surrounded by quotation marks or else your sounds will not be loaded.
- 'fallback_sound_library' refers to the path of where the selected fallback sound profile is located. In the case that you mistype the path of the original sound directory/profile, the program will redirect or "fall back" to a path of your choice.

Then, run kbmod by executing [in terminal]:
```sh
kbmod & disown
```

If you want to restart `kbmod`, do:
```sh
killall kbmod && kbmod & disown
```

Sometimes it helps to set a function for this. In my case I use the FISH shell, and this is how I wrote it [which is permanently set in a config file]:
```fish
function kbmod
  killall --wait kbmod 2>/dev/null
  command kbmod $argv &
  disown
end
```

Then running executing `kbmod` on its own restarts the program. Why do we ever need to restart kbmod? Because this is super helpful and often comes in clutch when you make a new edit in your configuration file as updating it does not automatically apply new changes. Qutting the terminal does not kill the program. For MacOS Golden Gate versions and up, you will see a '__Running in Background__' subheading below the title of your terminal. Force quitting the terminal via 'Stop Running in Background' will essentially kill the program.

Additionally, inside of the main directory you will see a readily-installed sound profile called `turquoise` which is originally obtained from kbsim's Github page under `src/assets/audio`. Make sure that the directory of your choice exists before moving or copying the sound profile:

```sh
mv turquoise path/of/your/choice
```

## Can I make my own sound profile/library? If so, what should it look like?

Of course you can, make sure you follow the correct namings of the files and file structure below:

```txt
+-example_sound_library
|
+-> press
|   |
|   +-> BACKSPACE.mp3
|   |
|   +-> ENTER.mp3
|   |
|   +-> GENERIC_R0.mp3
|   |
|   +-> GENERIC_R1.mp3
|   |
|   +-> GENERIC_R2.mp3
|   |
|   +-> GENERIC_R3.mp3
|   |
|   +-> GENERIC_R4.mp3
|   |
|   +-> SPACE.mp3
|
+-> release
    |
    +-> BACKSPACE.mp3
    |
    +-> ENTER.mp3
    |
    +-> GENERIC.mp3
    |
    +-> SPACE.mp3
```

The file structure and names must be exact or else the program will fail to load the sounds. R{number} here just means the 0-indexed row number from top to bottom on your keyboard.

## Why aren't key sounds being played for some keys like the Function keys?

To be honest with you, I tried to fix this issue and I could not do that myself. It would be really great to have someone contribute to the project and fix that.

## Why was this project created in the first place?

Mainly for C++ practice. But the real reason is because some mechanical keyboard simulator apps that I have downloaded to try out either have large latency (noticeable delay between the sound playing and the key being pressed) or do break within minutes. Thock is a great example for this. With the NK Creams sound profile, the sound that plays when you press two keys milliseconds apart from each other apparently glitches out the audio and so you would have to kill the Thock app in an attempt to restart it just to get it to work again (and then you experience the same thing again after a couple minutes have passed).

Some mechanical keyboard simulator apps are also hidden behind a paywall. You pay a low price and you get to keep the software forever but I have always found doubts in that and have always had the fear of being scammed.

There are also some apps out there that are similar to this one, but it is either their repositories moved or are completely removed + no backup of them to be found.

## Has AI assisted in this project?

Absolutely not. I do not condone the use of AI nor do I use it myself while programming in general.

Neither can you use it if you want to contribute code to this project.

## Sources to kbsim (tplai's project)

https://github.com/tplai/kbsim
