package codewatch

import (
	"fmt"
	"os"
	"regexp"
	"runtime"
	"strings"
	"syscall"
)

// FileFilter is used to remove unrelated files from the build.
type FileFilter func(filename string) bool

// CPPFilter includes any CPP source file in the watch.
func CPPFilter(filename string) bool {
	regex := regexp.MustCompile("^.*\\.(c|cpp|cc|cxx|h|hpp|hh|hxx)$")
	return regex.MatchString(filename)
}

/*
 * WatchBuild scans the build started by running buildCommand in a shell.
 * Pre: len(buildCommand) > 0
 * Returns an array of paths for the source code of the file.
 */
func WatchBuild(buildCommand []string, filter FileFilter) ([]string, error) {
	var filePaths []string

	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	pid, err := startPTrace(buildCommand)
	if err != nil {
		return filePaths, err
	}

	keepGoing := true
	for keepGoing {
		var status syscall.WaitStatus
		_, err = syscall.Wait4(pid, &status, 0, nil)
		if err != nil {
			return filePaths, err
		}

		if status.Stopped() {
			open, _, err := isOpenCall(pid)
			if err != nil {
				return filePaths, err
			} else if open {
				fmt.Println("have an open call!")
			}

			err = syscall.PtraceSyscall(pid, 0)
			if err != nil {
				return filePaths, err
			}
		} else {
			keepGoing = false
		}
	}

	return filePaths, nil
}

func isOpenCall(pid int) (bool, string, error) {
	var ptraceRegs syscall.PtraceRegs
	if err := syscall.PtraceGetRegs(pid, &ptraceRegs); err != nil {
		return false, "", err
	}

	if ptraceRegs.Orig_rax == 2 {
		var builder strings.Builder
		bytes := []byte{1}
		for i := 0; bytes[0] != 0; i++ {
			_, err := syscall.PtracePeekData(
				pid,
				uintptr(ptraceRegs.Rdi+uint64(i)),
				bytes,
			)
			if err != nil {
				return false, "", err
			}
			if bytes[0] != 0 {
				builder.WriteByte(bytes[0])
			}
		}
		return true, builder.String(), nil
	}

	return false, "", nil
}

/**
 * StartPTrace runs a ptrace'd command.
 * Pre: len(cmd) > 0
 */
func startPTrace(cmd []string) (int, error) {
	dir, err := os.Getwd()
	if err != nil {
		return -1, err
	}

	procAttr := syscall.ProcAttr{
		Dir:   dir,
		Env:   os.Environ(),
		Files: []uintptr{0, 1, 2},
		Sys: &syscall.SysProcAttr{
			Ptrace: true,
		},
	}

	return syscall.ForkExec(cmd[0], cmd, &procAttr)
}
