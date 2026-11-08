package org.opentoonz.mobile;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.GridLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.File;
import java.text.DateFormat;
import java.util.Date;
import java.util.List;

/** Project manager: list / create / open / rename / delete projects. */
public class ProjectsActivity extends Activity {
    static final int[][] SIZES = {{640, 360}, {960, 540}, {1280, 720}, {1920, 1080},
            {720, 720}, {540, 960}};
    static final String[] SIZE_NAMES = {"640×360 (low memory)", "960×540 (recommended)",
            "1280×720 (HD)", "1920×1080 (Full HD)", "720×720 (square)", "540×960 (vertical)"};
    static final int[] FPS = {8, 12, 15, 24, 25, 30};

    static final int REQ_TNZ_TREE = 11;
    private GridLayout mGrid;
    private TextView mEmpty;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        App.ensureEngine(getApplication());
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Ui.BG);

        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(Ui.PANEL);
        int p = Ui.dp(this, 12);
        bar.setPadding(p, p / 2, p / 2, p / 2);
        TextView title = Ui.label(this, getString(R.string.app_name) + " — " + getString(R.string.projects), 20);
        bar.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        ImageButton add = Ui.iconButton(this, R.drawable.ic_add, getString(R.string.new_project), 48);
        add.setColorFilter(Ui.ACCENT);
        add.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { newProjectDialog(); }
        });
        bar.addView(add);
        ImageButton imp = Ui.iconButton(this, R.drawable.ic_layers, getString(R.string.import_tnz), 48);
        imp.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_TNZ_TREE);
            }
        });
        bar.addView(imp);
        root.addView(bar);

        FrameLayout body = new FrameLayout(this);
        ScrollView sv = new ScrollView(this);
        mGrid = new GridLayout(this);
        mGrid.setPadding(p, p, p, p);
        sv.addView(mGrid);
        body.addView(sv);
        mEmpty = Ui.label(this, getString(R.string.no_projects), 16);
        mEmpty.setGravity(Gravity.CENTER);
        mEmpty.setTextColor(Ui.DIM);
        body.addView(mEmpty, new FrameLayout.LayoutParams(-1, -1));
        root.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration c) {
        super.onConfigurationChanged(c);
        refresh();
    }

    private void refresh() {
        mGrid.removeAllViews();
        List<File> projects = ProjectStore.list(this);
        mEmpty.setVisibility(projects.isEmpty() ? View.VISIBLE : View.GONE);
        int cardW = Ui.dp(this, 180);
        int cols = Math.max(1, getResources().getDisplayMetrics().widthPixels / (cardW + Ui.dp(this, 12)));
        mGrid.setColumnCount(cols);
        for (final File dir : projects) mGrid.addView(card(dir, cardW));
    }

    private View card(final File dir, int w) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(Ui.ripple(this, Ui.PANEL));
        int m = Ui.dp(this, 6);
        GridLayout.LayoutParams lp = new GridLayout.LayoutParams();
        lp.width = w;
        lp.setMargins(m, m, m, m);
        c.setLayoutParams(lp);
        c.setPadding(m, m, m, m);
        ImageView iv = new ImageView(this);
        iv.setScaleType(ImageView.ScaleType.FIT_CENTER);
        iv.setBackgroundColor(0xFF3A3F4E);
        Bitmap t = ProjectStore.thumbnail(dir);
        if (t != null) iv.setImageBitmap(t);
        c.addView(iv, new LinearLayout.LayoutParams(-1, w * 9 / 16));
        TextView name = Ui.label(this, dir.getName(), 15);
        name.setSingleLine(true);
        c.addView(name);
        TextView date = Ui.label(this, DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
                .format(new Date(ProjectStore.lastModified(dir))), 12);
        date.setTextColor(Ui.DIM);
        c.addView(date);
        c.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { open(dir, false); }
        });
        c.setOnLongClickListener(new View.OnLongClickListener() {
            @Override public boolean onLongClick(View v) { projectMenu(dir); return true; }
        });
        return c;
    }

    private void open(File dir, boolean isNew) {
        Intent i = new Intent(this, EditorActivity.class);
        i.putExtra(EditorActivity.EXTRA_DIR, dir.getAbsolutePath());
        i.putExtra(EditorActivity.EXTRA_NEW, isNew);
        startActivity(i);
    }

    private void projectMenu(final File dir) {
        String[] items = {getString(R.string.open), getString(R.string.rename), getString(R.string.delete)};
        new AlertDialog.Builder(this).setTitle(dir.getName())
                .setItems(items, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int which) {
                        if (which == 0) open(dir, false);
                        else if (which == 1) renameDialog(dir);
                        else deleteDialog(dir);
                    }
                }).show();
    }

    private void renameDialog(final File dir) {
        final EditText et = new EditText(this);
        et.setText(dir.getName());
        et.setInputType(InputType.TYPE_CLASS_TEXT);
        new AlertDialog.Builder(this).setTitle(R.string.rename).setView(et)
                .setPositiveButton(R.string.ok, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        File target = ProjectStore.newFolder(ProjectsActivity.this, et.getText().toString());
                        dir.renameTo(target);
                        refresh();
                    }
                }).setNegativeButton(R.string.cancel, null).show();
    }

    private void deleteDialog(final File dir) {
        new AlertDialog.Builder(this)
                .setMessage(getString(R.string.delete_confirm, dir.getName()))
                .setPositiveButton(R.string.delete, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        ProjectStore.deleteRecursive(dir);
                        refresh();
                    }
                }).setNegativeButton(R.string.cancel, null).show();
    }

    private void newProjectDialog() {
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(this, 16);
        l.setPadding(p, p / 2, p, 0);
        final EditText name = new EditText(this);
        name.setHint(R.string.name);
        name.setText("Animation");
        name.setSelectAllOnFocus(true);
        l.addView(name);
        l.addView(Ui.label(this, getString(R.string.canvas_size), 14));
        final Spinner size = new Spinner(this);
        size.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, SIZE_NAMES));
        size.setSelection(1);
        l.addView(size);
        l.addView(Ui.label(this, getString(R.string.fps), 14));
        final Spinner fps = new Spinner(this);
        String[] f = new String[FPS.length];
        for (int i = 0; i < FPS.length; i++) f[i] = FPS[i] + " fps";
        fps.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, f));
        fps.setSelection(1);
        l.addView(fps);
        new AlertDialog.Builder(this).setTitle(R.string.new_project).setView(l)
                .setPositiveButton(R.string.ok, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        int[] s = SIZES[size.getSelectedItemPosition()];
                        File dir = ProjectStore.newFolder(ProjectsActivity.this, name.getText().toString());
                        NativeEngine.newDocument(s[0], s[1], FPS[fps.getSelectedItemPosition()]);
                        NativeEngine.setName(dir.getName());
                        String err = NativeEngine.save(dir.getAbsolutePath());
                        if (err != null) {
                            new AlertDialog.Builder(ProjectsActivity.this)
                                    .setMessage(getString(R.string.save_failed, err)).show();
                            return;
                        }
                        ProjectStore.writeThumbnail(dir);
                        open(dir, true);
                    }
                }).setNegativeButton(R.string.cancel, null).show();
    }

    // ------------------------------------------------------------ .tnz import
    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_TNZ_TREE || res != RESULT_OK || data == null || data.getData() == null) return;
        final android.net.Uri tree = data.getData();
        final android.app.ProgressDialog pd = android.app.ProgressDialog.show(this, null,
                getString(R.string.copying), true, false);
        final android.os.Handler ui = new android.os.Handler(android.os.Looper.getMainLooper());
        new Thread(new Runnable() {
            @Override public void run() {
                final File dest = new File(getCacheDir(), "tnzimport");
                ProjectStore.deleteRecursive(dest);
                dest.mkdirs();
                String err = null;
                try {
                    String rootId = android.provider.DocumentsContract.getTreeDocumentId(tree);
                    copyTree(tree, rootId, dest, 0);
                } catch (Exception e) {
                    err = e.toString();
                }
                final List<File> scenes = new java.util.ArrayList<>();
                findTnz(dest, scenes);
                final String fErr = err;
                ui.post(new Runnable() {
                    @Override public void run() {
                        pd.dismiss();
                        if (fErr != null || scenes.isEmpty()) {
                            new AlertDialog.Builder(ProjectsActivity.this)
                                    .setMessage(fErr != null ? fErr : getString(R.string.no_tnz)).show();
                            return;
                        }
                        if (scenes.size() == 1) { importScene(scenes.get(0)); return; }
                        String[] names = new String[scenes.size()];
                        for (int i = 0; i < names.length; i++)
                            names[i] = scenes.get(i).getAbsolutePath().substring(dest.getAbsolutePath().length() + 1);
                        new AlertDialog.Builder(ProjectsActivity.this).setTitle(R.string.import_tnz_choose)
                                .setItems(names, new DialogInterface.OnClickListener() {
                                    @Override public void onClick(DialogInterface d, int which) {
                                        importScene(scenes.get(which));
                                    }
                                }).show();
                    }
                });
            }
        }).start();
    }

    private void copyTree(android.net.Uri tree, String docId, File dest, int depth) throws java.io.IOException {
        if (depth > 6) return;
        android.net.Uri children = android.provider.DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        String[] cols = {android.provider.DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                android.provider.DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                android.provider.DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (android.database.Cursor c = getContentResolver().query(children, cols, null, null, null)) {
            while (c != null && c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (name == null || name.startsWith(".")) continue;
                File f = new File(dest, name);
                if (android.provider.DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    f.mkdirs();
                    copyTree(tree, id, f, depth + 1);
                } else {
                    String lower = name.toLowerCase(java.util.Locale.ROOT);
                    if (!(lower.endsWith(".tnz") || lower.endsWith(".tlv") || lower.endsWith(".tpl")
                            || lower.endsWith(".pli") || lower.endsWith(".png"))) continue;
                    android.net.Uri u = android.provider.DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    try (java.io.InputStream in = getContentResolver().openInputStream(u);
                         java.io.OutputStream out = new java.io.FileOutputStream(f)) {
                        byte[] buf = new byte[1 << 16];
                        int n;
                        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                    }
                }
            }
        }
    }

    static void findTnz(File dir, List<File> out) {
        File[] kids = dir.listFiles();
        if (kids == null) return;
        for (File k : kids) {
            if (k.isDirectory()) findTnz(k, out);
            else if (k.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".tnz")) out.add(k);
        }
    }

    void importScene(final File tnz) {
        String err = NativeEngine.importTnz(tnz.getAbsolutePath());
        if (err != null) {
            new AlertDialog.Builder(this).setMessage(getString(R.string.load_failed, err)).show();
            return;
        }
        String name = tnz.getName().replaceAll("\\.tnz$", "");
        final File dir = ProjectStore.newFolder(this, name);
        NativeEngine.setName(dir.getName());
        err = NativeEngine.save(dir.getAbsolutePath());
        if (err != null) {
            new AlertDialog.Builder(this).setMessage(getString(R.string.save_failed, err)).show();
            return;
        }
        ProjectStore.writeThumbnail(dir);
        String[] notes = NativeEngine.getImportWarnings();
        if (notes.length == 0) {
            open(dir, false);
            return;
        }
        StringBuilder sb = new StringBuilder(getString(R.string.import_notes));
        for (String n : notes) sb.append("\n\u2022 ").append(n);
        new AlertDialog.Builder(this).setMessage(sb.toString())
                .setPositiveButton(R.string.open, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) { open(dir, false); }
                }).show();
    }
}
