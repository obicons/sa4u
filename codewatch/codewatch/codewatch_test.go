package codewatch

import (
	"syscall"
	"testing"
)

func TestCPPFilter(t *testing.T) {
	validFilenames := []string{
		"/my/path/to/example.c",
		"/my/path/to/example.cpp",
		"/my/path/to/example.cc",
		"/my/path/to/example.cxx",
		"/my/path/to/example.h",
		"/my/path/to/example.hpp",
		"/my/path/to/example.hh",
		"/my/path/to/example.hxx",
	}
	for _, filename := range validFilenames {
		if !CPPFilter(filename) {
			t.Fatalf("Expected %s to be valid", filename)
		}
	}

	invalidFilenames := []string{
		"/my/path/to/example.txt",
		"/my/path/to/example",
	}
	for _, filename := range invalidFilenames {
		if CPPFilter(filename) {
			t.Fatalf("Expected %s to be invalid", filename)
		}
	}
}

func TestStartPTrace(t *testing.T) {
	command := []string{"/bin/echo", "hello world"}
	pid, err := startPTrace(command)
	if err != nil {
		t.Fatalf("Error: %s", err)
	}
	// syscall.Kill(pid, syscall.SIGKILL)

	var status syscall.WaitStatus
	syscall.Wait4(pid, &status, 0, nil)
	syscall.PtraceCont(pid, 0)
	syscall.Wait4(pid, &status, 0, nil)
}

func TestWatchBuild(t *testing.T) {
	command := []string{"/bin/cat", "/etc/resolv.conf"}
	_, err := WatchBuild(command, CPPFilter)
	if err != nil {
		t.Fatalf("Error: %s", err)
	}
}
