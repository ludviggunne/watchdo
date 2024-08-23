# watchdo
Watches for changes on a set of files and runs commands on them.

## Usage
```
$ watchdo FLAGS... FILES... -- COMMAND
```
The FLAGS... correspond to (some of) the inotify watch flags,
for example -MODIFY <-> IN_MODIFY. Zero or more arguments in COMMAND
can be set to {}, and will be substituted with the filename for which
an event was received.

## Example
The following example prints the contents of modified files:
```
$ watchdo -MODIFY a.txt b.txt c.txt -- cat {}
```
