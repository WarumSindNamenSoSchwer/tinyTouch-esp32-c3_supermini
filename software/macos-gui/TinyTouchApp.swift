// tinyTouch Manager - native macOS GUI
//
// One window, four areas: device state, password, fingerprints, computers.
// All device work happens in a Python backend process that prints one JSON
// object per line; this file owns presentation and user interaction only.

import SwiftUI

// MARK: - Backend bridge

struct FingerInfo: Decodable, Equatable {
    let finger: Int
    let views: Int
}

struct BackendEvent: Decodable {
    let event: String
    var message: String?
    // status payload
    var helper_installed: Bool?
    var helper_loaded: Bool?
    var port: String?
    var device: [String: String]?
    var password_stored: Bool?
    var paired: Bool?
    var account: String?
    var error: String?
    // enrollment payload
    var slot: Int?
    var view: String?
    var tap: Int?
    var total: Int?
    // computers payload
    var ids: [String]?
    var capacity: Int?
    // fingers payload
    var fingers: [FingerInfo]?
    // logs payload
    var lines: [String]?
}

final class Backend {
    static let shared = Backend()

    private var scriptURL: URL {
        // The backend lives next to this source file in the repo. It imports the
        // repo's helper modules and needs the repo venv, so the app references it
        // in place instead of bundling a copy that would lose those neighbors.
        let source = URL(fileURLWithPath: #filePath)
        return source.deletingLastPathComponent().appendingPathComponent("tinytouch_gui_backend.py")
    }

    private var pythonURL: URL {
        // The repo venv has pyserial; fall back to system python3.
        let venv = scriptURL.deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent(".venv/bin/python")
        if FileManager.default.isExecutableFile(atPath: venv.path) { return venv }
        return URL(fileURLWithPath: "/usr/bin/python3")
    }

    /// Run one backend action, streaming events to the main actor.
    @discardableResult
    func run(_ arguments: [String], stdin: String? = nil,
             onEvent: @escaping (BackendEvent) -> Void) async -> Bool {
        let process = Process()
        process.executableURL = pythonURL
        process.arguments = [scriptURL.path] + arguments
        let output = Pipe()
        process.standardOutput = output
        process.standardError = Pipe()
        let input = Pipe()
        process.standardInput = input

        do { try process.run() } catch {
            await MainActor.run {
                onEvent(BackendEvent(event: "error", message: "Backend-Start fehlgeschlagen: \(error.localizedDescription)"))
            }
            return false
        }
        if let stdin {
            input.fileHandleForWriting.write(Data((stdin + "\n").utf8))
        }
        input.fileHandleForWriting.closeFile()

        var sawError = false
        do {
            for try await line in output.fileHandleForReading.bytes.lines {
                guard let data = line.data(using: .utf8),
                      let event = try? JSONDecoder().decode(BackendEvent.self, from: data) else { continue }
                if event.event == "error" { sawError = true }
                await MainActor.run { onEvent(event) }
            }
        } catch {
            // A torn pipe on process exit is not an application error.
        }
        process.waitUntilExit()
        return process.terminationStatus == 0 && !sawError
    }
}

// MARK: - App state

@MainActor
final class AppState: ObservableObject {
    @Published var device: [String: String]? = nil
    @Published var port: String? = nil
    @Published var account: String? = nil
    @Published var helperLoaded = false
    @Published var passwordStored = false
    @Published var paired = false
    @Published var statusError: String? = nil
    @Published var refreshing = false

    @Published var busy = false               // a device session is running
    @Published var banner: Banner? = nil      // transient instruction/result
    @Published var enrollProgress: (slot: Int, tap: Int, total: Int)? = nil
    @Published var fingers: [FingerInfo] = []
    @Published var computers: [String] = []
    @Published var computerCapacity = 8
    @Published var logLines: [String] = []

    struct Banner: Equatable {
        enum Kind { case info, touch, success, failure }
        let kind: Kind
        let text: String
    }

    var fingerprintCount: Int { Int(device?["fingerprints"] ?? "0") ?? 0 }
    var deviceConnected: Bool { device != nil }

    func refresh() async {
        refreshing = true
        defer { refreshing = false }
        await Backend.shared.run(["status"]) { [weak self] event in
            guard let self, event.event == "status" else { return }
            self.device = event.device
            self.port = event.port
            self.account = event.account
            self.helperLoaded = event.helper_loaded ?? false
            self.passwordStored = event.password_stored ?? false
            self.paired = event.paired ?? false
            self.statusError = event.error
        }
    }

    /// Shared event pump: turns backend events into banners.
    func pump(_ event: BackendEvent) {
        switch event.event {
        case "touch":
            banner = .init(kind: .touch, text: event.message ?? "Finger auflegen")
        case "retry":
            banner = .init(kind: .touch, text: event.message ?? "Nochmal versuchen")
        case "unlocked":
            banner = .init(kind: .info, text: "Autorisiert.")
        case "enroll_touch":
            enrollProgress = (event.slot ?? 0, event.tap ?? 1, event.total ?? 4)
            banner = .init(kind: .touch, text: event.message ?? "")
        case "enroll_lift":
            banner = .init(kind: .info, text: "Finger abheben")
        case "enroll_done":
            banner = .init(kind: .info,
                           text: "Scan \(event.slot ?? 0)/\(event.total ?? 4) gespeichert")
        case "typing":
            banner = .init(kind: .info, text: event.message ?? "")
        case "done":
            banner = .init(kind: .success, text: event.message ?? "Fertig.")
        case "error":
            banner = .init(kind: .failure, text: event.message ?? "Fehler")
        case "computers":
            computers = event.ids ?? []
            computerCapacity = event.capacity ?? 8
        case "fingers":
            fingers = event.fingers ?? []
        case "logs":
            logLines = event.lines ?? []
        default:
            break
        }
    }

    func perform(_ arguments: [String], stdin: String? = nil) async {
        guard !busy else { return }
        busy = true
        enrollProgress = nil
        await Backend.shared.run(arguments, stdin: stdin) { [weak self] in self?.pump($0) }
        enrollProgress = nil
        busy = false
        await refresh()
    }
}

// MARK: - Views

struct ContentView: View {
    @StateObject private var state = AppState()
    @State private var showPasswordSheet = false
    @State private var showDeleteConfirm = false
    @State private var showLogs = false

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            if let banner = state.banner {
                BannerView(banner: banner)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    deviceCard
                    passwordCard
                    fingerprintCard
                    computersCard
                }
                .padding(20)
            }
        }
        .frame(minWidth: 520, minHeight: 640)
        .animation(.easeInOut(duration: 0.2), value: state.banner)
        .task { await state.refresh() }
        .sheet(isPresented: $showPasswordSheet) { PasswordSheet(state: state) }
        .sheet(isPresented: $showLogs) { LogsSheet(state: state) }
        .confirmationDialog("Alle Fingerabdrücke löschen?",
                            isPresented: $showDeleteConfirm, titleVisibility: .visible) {
            Button("Alle löschen", role: .destructive) {
                Task { await state.perform(["delete", "all"]) }
            }
        } message: {
            Text("Danach kann das Gerät niemanden mehr erkennen, bis neu registriert wird.")
        }
    }

    private var header: some View {
        HStack(spacing: 12) {
            Image(systemName: "touchid")
                .font(.system(size: 28))
                .foregroundStyle(state.deviceConnected ? .green : .secondary)
            VStack(alignment: .leading, spacing: 2) {
                Text("tinyTouch").font(.title2.bold())
                Text(state.deviceConnected
                     ? "Verbunden · \(state.port ?? "")"
                     : (state.statusError ?? "Nicht verbunden"))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Button {
                showLogs = true
                Task { await Backend.shared.run(["logs"]) { state.pump($0) } }
            } label: { Image(systemName: "doc.text.magnifyingglass") }
            .help("Helper-Log anzeigen")
            Button {
                Task { await state.refresh() }
            } label: {
                if state.refreshing { ProgressView().controlSize(.small) }
                else { Image(systemName: "arrow.clockwise") }
            }
            .disabled(state.refreshing || state.busy)
        }
        .padding(16)
    }

    private var deviceCard: some View {
        Card(title: "Gerät", icon: "cpu") {
            if let device = state.device {
                Grid(alignment: .leading, horizontalSpacing: 16, verticalSpacing: 6) {
                    GridRow { label("Firmware"); value("\(device["firmware"] ?? "?") (\(device["build"] ?? "?"))") }
                    GridRow { label("Modus"); value((device["mode"] ?? "?").uppercased()) }
                    GridRow {
                        label("Sensor")
                        StatusDot(ok: device["sensor"] == "ready",
                                  text: device["sensor"] == "ready" ? "bereit" : "offline")
                    }
                    GridRow {
                        label("Tastatur (BLE)")
                        StatusDot(ok: device["keyboard"] == "connected",
                                  text: device["keyboard"] == "connected" ? "verbunden"
                                        : device["keyboard"] == "advertising" ? "sichtbar, nicht gekoppelt"
                                        : device["keyboard"] ?? "?")
                    }
                    GridRow {
                        label("Helper")
                        HStack(spacing: 8) {
                            StatusDot(ok: state.helperLoaded,
                                      text: state.helperLoaded ? "läuft (Autostart)" : "gestoppt")
                            Button(state.helperLoaded ? "Neu starten" : "Starten") {
                                Task { await state.perform(["helper", state.helperLoaded ? "restart" : "start"]) }
                            }
                            .controlSize(.small)
                        }
                    }
                }
            } else {
                Text("Gerät anschließen und aktualisieren.")
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var passwordCard: some View {
        Card(title: "Passwort", icon: "key.fill") {
            HStack {
                StatusDot(ok: state.passwordStored,
                          text: state.passwordStored
                                ? "Im Schlüsselbund hinterlegt"
                                : "Kein Passwort hinterlegt")
                Spacer()
                Button(state.passwordStored ? "Ändern…" : "Festlegen…") {
                    showPasswordSheet = true
                }
                .disabled(state.busy || !state.deviceConnected)
            }
            Text("Der Fingerabdruck tippt dieses Passwort in das gerade aktive Feld. Es liegt verschlüsselt im macOS-Schlüsselbund, nie auf dem Gerät.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private var fingerprintCard: some View {
        Card(title: "Fingerabdrücke", icon: "touchid") {
            HStack {
                StatusDot(ok: state.fingerprintCount > 0,
                          text: state.fingerprintCount > 0
                                ? "\(state.fingerprintCount) Scans auf dem Gerät"
                                : "Keine registriert")
                Spacer()
                Button("Finger laden") {
                    Task { await state.perform(["fingers"]) }
                }
                .disabled(state.busy || !state.deviceConnected)
                Button(role: .destructive) { showDeleteConfirm = true } label: {
                    Text("Alle löschen")
                }
                .disabled(state.busy || !state.deviceConnected || state.fingerprintCount == 0)
            }
            ForEach(state.fingers, id: \.finger) { info in
                HStack {
                    Image(systemName: "hand.point.up.left")
                    Text("Finger \(info.finger)")
                    Text(info.views > 0 ? "\(info.views) Scans" : "leer")
                        .font(.caption)
                        .foregroundStyle(info.views > 0 ? .green : .secondary)
                    Spacer()
                    Menu("Registrieren") {
                        Button("4 Scans (schnell)") {
                            Task { await state.perform(["enroll", String(info.finger), "4"]) }
                        }
                        Button("8 Scans (genauer)") {
                            Task { await state.perform(["enroll", String(info.finger), "8"]) }
                        }
                    }
                    .frame(width: 130)
                    .disabled(state.busy)
                    if info.views > 0 {
                        Button("Löschen", role: .destructive) {
                            Task { await state.perform(["delete", String(info.finger)]) }
                        }
                        .controlSize(.small)
                        .disabled(state.busy)
                    }
                }
                .padding(.vertical, 2)
            }
            if let progress = state.enrollProgress {
                EnrollmentProgress(slot: progress.slot, tap: progress.tap, total: progress.total)
            }
            HStack {
                Button("Tastaturtest") {
                    Task { await state.perform(["keyboard-test"]) }
                }
                .disabled(state.busy || !state.deviceConnected)
                Text("Tippt „tinytouch“ ins aktive Feld, nach Freigabe per Finger.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var computersCard: some View {
        Card(title: "Registrierte Computer", icon: "desktopcomputer") {
            HStack {
                StatusDot(ok: state.paired, text: state.paired
                          ? "Dieser Mac ist registriert"
                          : "Dieser Mac ist nicht registriert")
                Spacer()
                Button("Liste laden") {
                    Task { await state.perform(["computers"]) }
                }
                .disabled(state.busy || !state.deviceConnected)
                if !state.paired {
                    Button("Diesen Mac registrieren") {
                        Task { await state.perform(["pair"]) }
                    }
                    .disabled(state.busy || !state.deviceConnected)
                }
            }
            if !state.computers.isEmpty {
                ForEach(state.computers, id: \.self) { identifier in
                    HStack {
                        Image(systemName: "laptopcomputer")
                        Text(identifier).font(.system(.body, design: .monospaced))
                        Spacer()
                        Button("Entfernen", role: .destructive) {
                            Task { await state.perform(["remove-computer", identifier]) }
                        }
                        .controlSize(.small)
                        .disabled(state.busy)
                    }
                    .padding(.vertical, 2)
                }
                Text("\(state.computers.count) von \(state.computerCapacity) Plätzen belegt")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    private func label(_ text: String) -> some View {
        Text(text).foregroundStyle(.secondary).frame(width: 120, alignment: .leading)
    }
    private func value(_ text: String) -> some View {
        Text(text).textSelection(.enabled)
    }
}

// MARK: - Components

struct Card<Content: View>: View {
    let title: String
    let icon: String
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label(title, systemImage: icon).font(.headline)
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(RoundedRectangle(cornerRadius: 10).fill(.quaternary.opacity(0.5)))
    }
}

struct StatusDot: View {
    let ok: Bool
    let text: String
    var body: some View {
        HStack(spacing: 6) {
            Circle().fill(ok ? .green : .orange).frame(width: 8, height: 8)
            Text(text)
        }
    }
}

struct BannerView: View {
    let banner: AppState.Banner
    var body: some View {
        HStack(spacing: 10) {
            switch banner.kind {
            case .touch:
                Image(systemName: "hand.point.up.left.fill").foregroundStyle(.blue)
            case .info:
                Image(systemName: "info.circle.fill").foregroundStyle(.secondary)
            case .success:
                Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
            case .failure:
                Image(systemName: "xmark.octagon.fill").foregroundStyle(.red)
            }
            Text(banner.text).font(.callout)
            Spacer()
        }
        .padding(.horizontal, 16).padding(.vertical, 10)
        .background(banner.kind == .touch ? Color.blue.opacity(0.08) : Color.clear)
    }
}

struct EnrollmentProgress: View {
    let slot: Int
    let tap: Int
    let total: Int

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 6) {
                ForEach(1...max(total, 1), id: \.self) { index in
                    RoundedRectangle(cornerRadius: 4)
                        .fill(index < slot ? Color.green
                              : index == slot ? Color.blue : Color.secondary.opacity(0.2))
                        .frame(height: 6)
                }
            }
            Text("Scan \(slot)/\(total) · Berührung \(tap)/2")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(.top, 4)
    }
}

struct PasswordSheet: View {
    @ObservedObject var state: AppState
    @Environment(\.dismiss) private var dismiss
    @State private var password = ""
    @State private var confirmation = ""

    private var valid: Bool {
        !password.isEmpty && password == confirmation && password.utf8.count <= 160
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Mac-Passwort hinterlegen").font(.headline)
            Text("Dieses Passwort tippt tinyTouch nach einem erkannten Fingerabdruck. Es wird nur im Schlüsselbund dieses Macs gespeichert.")
                .font(.caption).foregroundStyle(.secondary)
            SecureField("Passwort", text: $password)
            SecureField("Passwort wiederholen", text: $confirmation)
            if !password.isEmpty && password != confirmation {
                Text("Passwörter stimmen nicht überein.").font(.caption).foregroundStyle(.red)
            }
            HStack {
                Spacer()
                Button("Abbrechen") { dismiss() }
                Button("Speichern") {
                    let value = password
                    password = ""; confirmation = ""
                    dismiss()
                    Task { await state.perform(["set-password"], stdin: value) }
                }
                .keyboardShortcut(.defaultAction)
                .disabled(!valid)
            }
        }
        .padding(20)
        .frame(width: 380)
    }
}

struct LogsSheet: View {
    @ObservedObject var state: AppState
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Helper-Log").font(.headline)
                Spacer()
                Button("Schließen") { dismiss() }
            }
            ScrollView {
                Text(state.logLines.joined(separator: "\n"))
                    .font(.system(.caption, design: .monospaced))
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .padding(16)
        .frame(width: 640, height: 420)
    }
}

// MARK: - Entry

@main
struct TinyTouchApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .windowResizability(.contentSize)
    }
}
