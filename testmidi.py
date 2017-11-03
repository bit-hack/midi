from __future__ import print_function
import os
import subprocess


exe_path = r'C:/repos/midi/build/Debug/miditool.exe'
test_path = r'data'
failed = []


def on_midi_file(midi_path):
    global failed
    print('{0}'.format(midi_path))
    ret = subprocess.call([exe_path, midi_path])
    if ret == 1:
        failed += [midi_path]


def main():
    tested = 0
    for root, dirs, files in os.walk("data"):
        for f in files:
            if f.lower().endswith(".mid"):
                tested += 1
                on_midi_file(os.path.join(root, f))
    print('{0} of {1} passed'.format(tested-len(failed), tested))
    for test in failed:
        print(' ! {0}'.format(test))


if __name__ == '__main__':
    main()
