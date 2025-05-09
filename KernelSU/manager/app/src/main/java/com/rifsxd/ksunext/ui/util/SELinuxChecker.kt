package com.rifsxd.ksunext.ui.util

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.topjohnwu.superuser.Shell
import com.rifsxd.ksunext.R

@Composable
fun getSELinuxStatus(): String {
    val shell = Shell.getShell() // Get the default shell instance
    val list = ArrayList<String>()
    val result = shell.newJob()
        .add("getenforce")
        .to(list, list)
        .exec()
    val output = list.joinToString("\n").trim()

    if (result.isSuccess) {
        return when (output) {
            "Enforcing" -> stringResource(R.string.selinux_status_enforcing)
            "Permissive" -> stringResource(R.string.selinux_status_permissive)
            "Disabled" -> stringResource(R.string.selinux_status_disabled)
            else -> stringResource(R.string.selinux_status_unknown)
        }
    }

    return if (output.endsWith("Permission denied")) {
        stringResource(R.string.selinux_status_enforcing)
    } else {
        stringResource(R.string.selinux_status_unknown)
    }
}
