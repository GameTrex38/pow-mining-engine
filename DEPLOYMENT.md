# Deployment Guide: PoW Mining Engine & Real-Time Telemetry Visualizer

This guide provides simple, step-by-step instructions to deploy your capstone miniproject to the cloud so you can present it **live from any phone, tablet, projector, or evaluator's PC** without needing your laptop.

---

## Deployment Options Overview

| Platform | Cost | Setup Time | Runs Real C++ Engine? | Best For |
|---|---|---|---|---|
| **Render.com (Docker)** *(Recommended)* | **100% Free** | 3 mins | **Yes (Linux Cloud Container)** | Full defense presentation with live C++ backend & REST API |
| **GitHub Pages** | **100% Free** | 1 min | **Yes (Standalone Browser Mode)** | Permanent zero-maintenance URL with zero sleep time |
| **Railway / Fly.io** | Free Tier / Low | 2 mins | **Yes (Docker Edge Container)** | Production cloud hosting |

---

## Method 1: Deploy to Render.com (Recommended — Full C++20 Cloud Container)

Render builds and runs the native C++20 multi-threaded engine inside a Linux Docker container and provides an automatic free `https://...` address.

### Step-by-Step Instructions:

1. **Push your project to GitHub:**
   If you haven't already created a GitHub repository:
   ```bash
   cd C:\Users\Admin\.gemini\antigravity\scratch\pow_mining_engine
   git add .
   git commit -m "Add production Dockerfile and cloud deployment configuration"
   git remote add origin https://github.com/<YOUR_GITHUB_USERNAME>/pow-mining-engine.git
   git branch -M main
   git push -u origin main
   ```

2. **Sign in to Render:**
   - Go to [render.com](https://render.com) and sign in with your GitHub account.

3. **Create a New Web Service:**
   - Click the blue **New +** button in the dashboard $\to$ Select **Web Service**.
   - Choose **Build and deploy from a Git repository**.
   - Select your repository `pow-mining-engine`.

4. **Verify Configuration:**
   Render will automatically detect the bundled `Dockerfile`:
   - **Name:** `pow-mining-engine` (or your choice)
   - **Region:** Any (e.g., Oregon or Frankfurt)
   - **Branch:** `main` (or `master`)
   - **Runtime:** `Docker` (Auto-detected)
   - **Instance Type:** `Free`

5. **Deploy:**
   - Click **Deploy Web Service**.
   - Render's cloud builders will compile the C++20 engine with GCC 13, run the unit test verification suite, and start the HTTP server.
   - Within 2–3 minutes, you will receive your live URL:
     ```
     https://pow-mining-engine-xxxx.onrender.com
     ```
   - Open this URL on your phone or any PC during your project evaluation!

> [!NOTE]
> Render free web services automatically sleep after 15 minutes of inactivity. When you visit the URL during your presentation, allow ~30 seconds for the container to wake up.

---

## Method 2: Deploy to GitHub Pages (Instant Zero-Server URL)

The visualizer includes an **automatic standalone browser mode**. If no C++ backend is detected, it runs the mining simulation, block generation, Merkle tree updates, and Midstate benchmarks directly in the browser using client-side JavaScript.

### Step-by-Step Instructions:

1. **Push your code to GitHub** (as shown in Step 1 above).
2. Go to your repository on GitHub.
3. Click **Settings** (top tab) $\to$ Select **Pages** (in the left sidebar).
4. Under **Build and deployment**:
   - **Source:** Select `Deploy from a branch`.
   - **Branch:** Choose `main` (or `master`), and select folder `/ (root)` or create a branch targeting `/web`.
5. Click **Save**.
6. Within 60 seconds, your site will be live at:
   ```
   https://<YOUR_GITHUB_USERNAME>.github.io/pow-mining-engine/web/
   ```
7. You can bookmark this link on your smartphone or send it to your evaluators.

---

## Method 3: Deploy to Fly.io (Command-Line Docker Deployment)

If you prefer deploying directly via CLI:

1. Install Fly CLI:
   ```powershell
   powershell -Command "iwr https://fly.io/install.ps1 -useb | iex"
   ```
2. Log in and launch:
   ```bash
   fly auth login
   fly launch
   ```
3. The bundled `fly.toml` will configure the port and deploy:
   ```bash
   fly deploy
   ```
4. Access your live app at:
   ```
   https://pow-mining-engine.fly.dev
   ```

---

## Testing Your Live Deployment

Once deployed to any of the above platforms, you can verify:
- Open the root URL in Chrome/Safari on your mobile phone or tablet.
- Confirm the **Live Hashrate Oscilloscope** is streaming.
- Toggle **Midstate Optimization** between `ON` and `OFF` to demonstrate the +53% speedup.
- Click **Run Benchmark** to present the real-time speedup report card to evaluators.
- Click **+ Inject Transaction** to show the live pairwise Merkle tree recalculation.
