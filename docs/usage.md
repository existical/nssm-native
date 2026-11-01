# Basic usage

Run these commands from the directory containing your built `nssm.exe`. Use an
elevated terminal when installing, removing or changing a Windows service.

```powershell
# Show version and architecture.
./nssm.exe version

# Install a service using the graphical installer.
./nssm.exe install MyService

# Install directly with an executable and its arguments.
./nssm.exe install MyService 'C:\Apps\worker.exe' --config 'C:\Apps\worker.ini'

# Start, inspect and stop it.
./nssm.exe start MyService
./nssm.exe status MyService
./nssm.exe stop MyService

# Open the graphical editor.
./nssm.exe edit MyService

# Remove the service, accepting the removal confirmation.
./nssm.exe remove MyService confirm
```

NSSM supports application arguments, startup directories, service accounts,
environment settings, restart policies, output redirection, log rotation and
event hooks. The preserved [upstream manual](upstream/README.txt) contains the
complete command and configuration reference, along with author and contributor
credits. Its old compiler and Windows compatibility notes describe upstream
releases; see [current build requirements](building.md) for this continuation.
