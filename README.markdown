# watchdo
Watches for changes on a set of files and runs commands on them.

## Usage
```
$ watchdo FLAGS... FILES... -- COMMAND
```
The FLAGS... correspond to (some of) the inotify watch flags, for example
-MODIFY corresponds to IN_MODIFY (see man inotify). Any occurence of the
sequence '{}' in COMMAND will be substituted with the filename for which an
event was received.

## Example
The following example formats C source files on write:
```
$ watchdo -CLOSE_WRITE *.c *.h -- indent -linux {}
```
