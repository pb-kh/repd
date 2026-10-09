return rp.module { description = "SSH client", files = { { dest = "~/.ssh/config", content = "Host *\n", mode = "0600" } } }
