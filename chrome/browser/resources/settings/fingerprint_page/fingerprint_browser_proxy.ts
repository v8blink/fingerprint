export interface FingerprintRow {
  id: string;
  label: string;
}

export interface FingerprintBrowserProxy {
  initializeFingerprint(): void;
  openFingerprintTab(id: string): void;
}

export class FingerprintBrowserProxyImpl implements FingerprintBrowserProxy {
  initializeFingerprint() {
    chrome.send('initializeFingerprint');
  }

  openFingerprintTab(id: string) {
    chrome.send('openFingerprintTab', [id]);
  }

  static getInstance(): FingerprintBrowserProxy {
    return instance || (instance = new FingerprintBrowserProxyImpl());
  }

  static setInstance(obj: FingerprintBrowserProxy) {
    instance = obj;
  }
}

let instance: FingerprintBrowserProxy|null = null;
