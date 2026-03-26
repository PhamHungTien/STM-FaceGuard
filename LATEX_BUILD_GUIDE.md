# LaTeX Build & PDF Generation Guide

**Quick Start**: Follow this guide to compile your thesis to PDF and validate all improvements.

---

## 🖥️ System Requirements

- **macOS** (already have)
- **Homebrew** (package manager)
- **MacTeX** (~4.5 GB, 10-15 min install)
- **Terminal** (built-in)

---

## Step 1: Check if LaTeX Already Installed

```bash
which xelatex
which biber
```

**Expected output**:
- ✅ If you see paths like `/usr/local/bin/xelatex`, LaTeX is ready → Skip to Step 3
- ❌ If no output, continue to Step 2

---

## Step 2: Install MacTeX via Homebrew

### 2a. Install Homebrew (if not already installed)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 2b. Install MacTeX

```bash
brew install --cask mactex
```

**Timeline**: ~10-15 minutes (downloads ~4.5 GB)

**Verify installation**:
```bash
xelatex --version
biber --version
```

Expected:
```
XeTeX 3.141592653-2.6 ...
biber version: 2.19
```

---

## Step 3: Build Your Thesis PDF

### 3a. Navigate to thesis directory

```bash
cd ~/Documents/STM-FaceGuard/thesis
```

### 3b. Clean previous build artifacts (optional)

```bash
rm -f main.pdf main.aux main.log main.bbl main.blg main.toc *.out
```

### 3c. Run LaTeX build sequence

**First pass – compile document structure**:
```bash
xelatex -synctex=1 -interaction=nonstopmode main.tex
```

Expected output ends with: `Output written on main.pdf ...` (or similar)

**Process bibliography references**:
```bash
biber main
```

Expected: `WARN - Duplicate entry in bib source` (if any, can ignore) + `processed` counts

**Second pass – resolve cross-references**:
```bash
xelatex -synctex=1 -interaction=nonstopmode main.tex
```

**Third pass – finalize layout**:
```bash
xelatex -synctex=1 -interaction=nonstopmode main.tex
```

### 3d. Verify PDF was created

```bash
ls -lh main.pdf
```

Expected:
```
-rw-r--r--  1 user  staff  2.5M Mar 26 14:23 main.pdf
```

---

## Step 4: Open and Review PDF

```bash
open main.pdf
```

Or manually open in **Preview** / **Adobe Reader**:
- Check all diagrams rendered (FSM, Block diagram, Timing)
- Verify all cross-references resolved (no `??` symbols)
- Scroll through all chapters to spot any formatting issues

---

## ✅ Success Checklist

After opening `main.pdf`:

- [ ] **No compilation errors** (build completed successfully)
- [ ] **No undefined references** (all `\cref{}` and `\label{}` matched)
- [ ] **All diagrams visible**:
  - FSM State Machine (Ch.4)
  - System Block Diagram (Appendix A)
  - UART Timing Diagram (Appendix A)
  - End-to-End Latency Timeline (Appendix A)
- [ ] **All tables formatted correctly**:
  - Related Work comparison (Ch.2)
  - Voting comparison (Ch.5)
  - Security metrics (Ch.4)
- [ ] **Code listings printed properly** (Appendix B)
- [ ] **Page count reasonable** (should be 80-120 pages)

---

## 🔧 Troubleshooting

### Error: "Package 'tikz' not found"

**Solution**: `tikz` is included with MacTeX, but may need update:
```bash
tlmgr update --self --all
```

### Error: "Undefined control sequence"

**Cause**: Missing LaTeX package  
**Solution**: Rebuild with:
```bash
xelatex -halt-on-error main.tex
```

(Shows exact line number of error), then check syntax in that file

### Error: "siunitx package not found"

**Solution**: Install full MacTeX (not BasicTeX):
```bash
brew install --cask mactex  # (not mactex-no-gui)
```

### Warning: "Citation undefined: ..."

**Solution**: This is normal on first pass. Run `biber main` then `xelatex` again.

### PDF doesn't update after edits

**Solution**:
1. Close PDF preview
2. Delete `main.pdf`
3. Rebuild from Step 3c

---

## 📊 Build Output Explanation

### Typical successful build output:

```
This is XeTeX, Version 3.141592653-2.6 (TeX Live 2024/Homebrew)
  ...
  (./main.aux)
  ...
  Output written on main.pdf (122 pages, 2567890 bytes).
  Transcript written on main.log.
```

**Key indicators**:
- ✅ `Output written on main.pdf` = Success
- ✅ Page count appears = Document compiled fully
- ✅ Zero `! Undefined control sequence` = No syntax errors

### Post-biber output:

```
INFO - Processing entry 'SimonChan2021'
...
WARN - ...
INFO - 23 entries processed, 0 warnings and 0 crossref errors
```

**Normal**: Some WARN messages are OK if processing count is high

---

## 🎯 Custom Build Scripts (Optional)

Save as `thesis/rebuild.sh`:

```bash
#!/bin/bash
echo "Cleaning old artifacts..."
rm -f main.pdf main.aux main.log main.bbl main.blg main.toc *.out

echo "Running XeLaTeX (pass 1)..."
xelatex -synctex=1 -interaction=nonstopmode main.tex > /dev/null

echo "Processing bibliography..."
biber main > /dev/null

echo "Running XeLaTeX (pass 2)..."
xelatex -synctex=1 -interaction=nonstopmode main.tex > /dev/null

echo "Running XeLaTeX (pass 3)..."
xelatex -synctex=1 -interaction=nonstopmode main.tex > /dev/null

echo "✅ Build complete! Opening PDF..."
open main.pdf
```

**Usage**:
```bash
cd ~/Documents/STM-FaceGuard/thesis
chmod +x rebuild.sh
./rebuild.sh
```

---

## ☁️ Alternative: Build Online (No Installation)

### Using Overleaf.com:

1. Go to [overleaf.com](https://www.overleaf.com) and create account
2. Click "New Project" → "Upload Project"
3. Find `STM-FaceGuard/thesis` folder on your Mac
4. Compress to `.zip` and upload
5. Overleaf automatically compiles
6. Download PDF directly

**Advantages**:
- ✅ No installation needed
- ✅ Live preview
- ✅ Easy sharing with advisor

**Disadvantages**:
- ❌ Need account (free tier ok)
- ❌ Need internet
- ❌ File size limits

---

## 📝 After Building PDF

### For Submission:

1. **Verify content** (as per ✅ checklist above)
2. **Fill test data**: Replace `[TBD]` in Ch.5 with actual measurements
3. **Final proofread**: Check Vietnamese grammar/spelling
4. **Rename file**: 
   ```bash
   cp main.pdf STM-FaceGuard-Thesis-Final.pdf
   ```
5. **Archive backup**:
   ```bash
   zip -r STM-FaceGuard-Thesis-Final.zip .
   ```

---

## 📞 Still Having Issues?

### Check these files:

- **Main config**: `thesis/main.tex` - Document structure
- **Chapter files**: `thesis/chapters/ch*.tex` - Content
- **Appendix**: `thesis/appendix/*.tex` - Diagrams & code
- **Build log**: `main.log` - Detailed error messages

### Common next steps:

```bash
# View build log for error details
tail -100 main.log

# Check specific chapter syntax
xelatex -halt-on-error chapters/ch4_software.tex
```

---

## ✨ Done!

Once `main.pdf` builds successfully with no errors:

🎉 **Thesis is ready for submission!**

---

*Generated: 26 March 2025*  
*For STM-FaceGuard thesis project*
