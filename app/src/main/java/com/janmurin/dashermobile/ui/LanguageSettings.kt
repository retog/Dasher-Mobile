package com.janmurin.dashermobile.ui

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.res.vectorResource
import androidx.compose.ui.unit.dp
import com.janmurin.dashermobile.DasherLanguage
import com.janmurin.dashermobile.PersonalDictionary
import com.janmurin.dashermobile.R
import com.janmurin.dashermobile.TrainingTexts

/** The languages of a set, by name: "Deutsch, English". */
fun languageNames(languages: Set<DasherLanguage>): String =
    languages.sortedBy { it.code }.joinToString(", ") { it.label }

/**
 * Dialog for choosing the languages Dasher predicts for.  Any number can be chosen, but at
 * least one: the last one chosen cannot be unchecked.
 *
 * @param current   The languages chosen so far.
 * @param onDismiss Called to close the dialog without a change.
 * @param onConfirm Called with the new choice.
 */
@Composable
fun LanguagesDialog(
    current: Set<DasherLanguage>,
    onDismiss: () -> Unit,
    onConfirm: (Set<DasherLanguage>) -> Unit
) {
    var chosen by remember { mutableStateOf(current) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = stringResource(id = R.string.select_language)) },
        text = {
            Column {
                Text(
                    text = stringResource(id = R.string.select_language_hint),
                    style = MaterialTheme.typography.bodyMedium
                )
                DasherLanguage.values().sortedBy { it.label }.forEach { language ->
                    val checked = language in chosen
                    val toggle = {
                        if (!checked) {
                            chosen = chosen + language
                        } else if (chosen.size > 1) {
                            chosen = chosen - language
                        }
                    }
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable(onClick = toggle)
                            .padding(vertical = 2.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Checkbox(checked = checked, onCheckedChange = { toggle() })
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(text = language.label)
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(chosen) }) {
                Text(text = stringResource(id = R.string.ok))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(id = R.string.cancel))
            }
        }
    )
}

/**
 * Dialog listing, for each chosen language, where its training text comes from, with a
 * button to import one from a file.
 *
 * @param languages The chosen languages.
 * @param onDismiss Called to close the dialog.
 */
@Composable
fun TrainingTextsDialog(languages: Set<DasherLanguage>, onDismiss: () -> Unit) {
    val context = LocalContext.current
    var importingFor by remember { mutableStateOf<DasherLanguage?>(null) }
    // Bumped after an import, to read the sources again.
    var imports by remember { mutableStateOf(0) }

    val importLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        val language = importingFor
        importingFor = null
        if (uri == null || language == null) return@rememberLauncherForActivityResult
        val imported = TrainingTexts.import(context, language, uri)
        imports++
        val messageRes = if (imported) R.string.training_text_import_success else R.string.training_text_import_failure
        Toast.makeText(context, context.getString(messageRes), Toast.LENGTH_SHORT).show()
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = stringResource(id = R.string.settings_training_texts)) },
        text = {
            Column {
                Text(
                    text = stringResource(id = R.string.training_texts_hint),
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(modifier = Modifier.size(8.dp))
                languages.sortedBy { it.label }.forEach { language ->
                    val source = remember(language, imports) { TrainingTexts.source(context, language) }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(text = language.label)
                            Text(
                                text = stringResource(
                                    id = when (source) {
                                        TrainingTexts.Source.BUNDLED -> R.string.training_text_bundled
                                        TrainingTexts.Source.IMPORTED -> R.string.training_text_imported
                                        TrainingTexts.Source.NONE -> R.string.training_text_none
                                    }
                                ),
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                        TextButton(onClick = {
                            importingFor = language
                            importLauncher.launch(arrayOf("text/*"))
                        }) {
                            Text(text = stringResource(id = R.string.training_text_import))
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(id = R.string.close))
            }
        }
    )
}

/**
 * Dialog showing the personal dictionary: the words and phrases learned from the user's
 * writing and those added by hand.  Entries can be added and removed, and everything learned
 * can be deleted.
 *
 * @param languages The chosen languages, which a new entry can be added for.
 * @param onDismiss Called to close the dialog.
 */
@Composable
fun PersonalDictionaryDialog(languages: Set<DasherLanguage>, onDismiss: () -> Unit) {
    val context = LocalContext.current
    var entries by remember { mutableStateOf(PersonalDictionary.entries(context)) }
    var newText by remember { mutableStateOf("") }
    var forAllLanguages by remember { mutableStateOf(true) }
    var confirmForget by remember { mutableStateOf(false) }
    val languageSet = DasherLanguage.formatSet(languages)

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = stringResource(id = R.string.settings_personal_dictionary)) },
        text = {
            Column {
                Text(
                    text = stringResource(id = R.string.dictionary_hint),
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(modifier = Modifier.size(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = newText,
                        onValueChange = { newText = it },
                        label = { Text(text = stringResource(id = R.string.dictionary_add_label)) },
                        singleLine = true,
                        modifier = Modifier.weight(1f)
                    )
                    TextButton(
                        enabled = newText.isNotBlank(),
                        onClick = {
                            if (PersonalDictionary.add(context, newText, if (forAllLanguages) "" else languageSet)) {
                                newText = ""
                                entries = PersonalDictionary.entries(context)
                            }
                        }
                    ) {
                        Text(text = stringResource(id = R.string.dictionary_add))
                    }
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { forAllLanguages = true },
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    RadioButton(selected = forAllLanguages, onClick = { forAllLanguages = true })
                    Text(text = stringResource(id = R.string.dictionary_all_languages))
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { forAllLanguages = false },
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    RadioButton(selected = !forAllLanguages, onClick = { forAllLanguages = false })
                    Text(text = stringResource(id = R.string.dictionary_current_languages, languageNames(languages)))
                }
                if (entries.isEmpty()) {
                    Text(
                        text = stringResource(id = R.string.dictionary_empty),
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(vertical = 12.dp)
                    )
                }
                LazyColumn(modifier = Modifier.heightIn(max = 240.dp)) {
                    items(entries, key = { it.languages + "\t" + it.text }) { entry ->
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(text = entry.text)
                                Text(text = entryDetails(entry), style = MaterialTheme.typography.bodySmall)
                            }
                            IconButton(onClick = {
                                PersonalDictionary.remove(context, entry)
                                entries = PersonalDictionary.entries(context)
                            }) {
                                Icon(
                                    imageVector = ImageVector.vectorResource(id = R.drawable.delete_24px),
                                    contentDescription = stringResource(id = R.string.icon_delete),
                                    modifier = Modifier.size(20.dp)
                                )
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(id = R.string.close))
            }
        },
        dismissButton = {
            TextButton(onClick = { confirmForget = true }) {
                Text(text = stringResource(id = R.string.dictionary_forget))
            }
        }
    )

    if (confirmForget) {
        AlertDialog(
            onDismissRequest = { confirmForget = false },
            text = { Text(text = stringResource(id = R.string.dictionary_forget_confirm)) },
            confirmButton = {
                TextButton(onClick = {
                    PersonalDictionary.forgetLearned(context)
                    entries = PersonalDictionary.entries(context)
                    confirmForget = false
                }) {
                    Text(text = stringResource(id = R.string.dictionary_forget))
                }
            },
            dismissButton = {
                TextButton(onClick = { confirmForget = false }) {
                    Text(text = stringResource(id = R.string.cancel))
                }
            }
        )
    }
}

/** How an entry came to be and which languages it is for: "written 4× · Deutsch, English". */
@Composable
private fun entryDetails(entry: PersonalDictionary.Entry): String {
    val origin = if (entry.manual && entry.count == 0) {
        stringResource(id = R.string.dictionary_entry_added)
    } else {
        stringResource(id = R.string.dictionary_entry_learned, entry.count)
    }
    val languages = if (entry.languages.isEmpty()) {
        stringResource(id = R.string.dictionary_all_languages)
    } else {
        languageNames(DasherLanguage.parseSet(entry.languages))
    }
    return "$origin · $languages"
}
