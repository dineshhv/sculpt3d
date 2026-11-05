package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Add
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.Description
import androidx.compose.material.icons.outlined.FolderOpen
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.higglerslab.sculpt3d.R
import com.higglerslab.sculpt3d.core.SculptNative
import com.higglerslab.sculpt3d.session.Documents
import com.higglerslab.sculpt3d.session.RecentDocument
import java.text.DateFormat
import java.util.Date

/** The first page: the brand mark with New and Open, recent documents beside or below (Swift WelcomeView). */
@Composable
fun WelcomeScreen(onOpenRecent: (RecentDocument) -> Unit) {
    val host = LocalHost.current
    val wide = LocalWide.current
    val dark = MaterialTheme.colorScheme.background.red < 0.5f
    val bg = if (dark) Brush.verticalGradient(listOf(Color(0xFF14161C), Color(0xFF0D1330)))
    else Brush.verticalGradient(listOf(Color.White, Color(0xFFE8EDF8)))
    val brand: @Composable () -> Unit = {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Image(painterResource(R.drawable.brand_mark), "Sculpt3D", Modifier.size(if (wide) 132.dp else 104.dp).shadow(0.dp))
            Text("Sculpt3D", fontSize = if (wide) 46.sp else 38.sp, fontWeight = FontWeight.Bold,
                color = if (dark) Color(0xFFDCE3FF) else Brand.ink)
            Text("Version ${SculptNative.version()}", fontSize = 13.sp, color = Color.Gray)
            Spacer(Modifier.height(8.dp))
            Button(onClick = { host.newDocument() }, modifier = Modifier.width(240.dp)) {
                Icon(Icons.Outlined.Add, null); Spacer(Modifier.width(6.dp)); Text("New Document")
            }
            OutlinedButton(onClick = { host.openDocument() }, modifier = Modifier.width(240.dp)) {
                Icon(Icons.Outlined.FolderOpen, null); Spacer(Modifier.width(6.dp)); Text("Open…")
            }
            if (Documents.sessions.isNotEmpty()) {
                TextButton(onClick = { WelcomeState.visible = false }) {
                    Text("Back to ${Documents.active?.displayName ?: "document"}")
                }
            }
        }
    }
    val recents: @Composable (Modifier) -> Unit = { m ->
        Column(m) {
            Text("Recent", fontWeight = FontWeight.SemiBold, fontSize = 17.sp, modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))
            if (Documents.recents.isEmpty()) {
                Text("Documents you open will appear here.", fontSize = 14.sp, color = Color.Gray, modifier = Modifier.padding(horizontal = 16.dp))
            } else {
                LazyColumn {
                    items(Documents.recents, key = { it.uri.toString() }) { r ->
                        Row(
                            Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).clickable { onOpenRecent(r) }.padding(horizontal = 16.dp, vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Icon(Icons.Outlined.Description, null, Modifier.size(30.dp), tint = MaterialTheme.colorScheme.primary)
                            Spacer(Modifier.width(10.dp))
                            Column(Modifier.weight(1f)) {
                                Text(r.name, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                Text(
                                    (if (r.uri.scheme == "file") "Autosaved · " else "") +
                                        DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(r.time)),
                                    fontSize = 12.sp, color = Color.Gray, maxLines = 1,
                                )
                            }
                            IconButton(onClick = { Documents.removeRecent(r) }) { Icon(Icons.Outlined.Close, "Remove from Recent", Modifier.size(18.dp)) }
                        }
                    }
                }
            }
        }
    }
    Box(Modifier.fillMaxSize().background(bg).safeDrawingPadding()) {
        if (wide) {
            Row(Modifier.fillMaxSize()) {
                Box(Modifier.weight(1f).fillMaxHeight(), contentAlignment = Alignment.Center) { brand() }
                recents(Modifier.width(340.dp).fillMaxHeight().background(MaterialTheme.colorScheme.surface).padding(top = 32.dp))
            }
        } else {
            Column(Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally) {
                Spacer(Modifier.height(40.dp))
                brand()
                Spacer(Modifier.height(24.dp))
                recents(Modifier.fillMaxWidth().weight(1f).clip(RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp))
                    .background(MaterialTheme.colorScheme.surface).padding(top = 8.dp))
            }
        }
    }
}

/** Whether the welcome page shows over the open documents. */
object WelcomeState {
    var visible by androidx.compose.runtime.mutableStateOf(true)
}
