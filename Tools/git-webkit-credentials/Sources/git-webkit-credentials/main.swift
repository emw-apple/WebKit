import Foundation
import Security

enum Service: String {
    case bugzilla = "https://bugs.webkit.org"
    case github = "https://api.github.com"
}

enum Item: RawRepresentable {
    case username
    case credential(forUsername: String)
    
    init?(rawValue: String) {
        if rawValue == "username" {
            self = .username
        } else {
            self = .credential(forUsername: rawValue)
        }
    }
    
    var rawValue: String {
        switch self {
        case .username: return "username"
        case .credential(let username): return username
        }
    }
}

func get(item: Item, for service: Service) -> String? {
    let query: [CFString: Any] = [
        kSecClass: kSecClassGenericPassword,
        kSecAttrService: service.rawValue,
        kSecAttrAccount: item.rawValue,
        kSecReturnAttributes: true,
        kSecReturnData: true,
    ]

    var item: CFTypeRef?
    let status = SecItemCopyMatching(query as CFDictionary, &item)
    guard status != errSecItemNotFound else {
        fputs("\(CommandLine.arguments[0]): \((service, item)) not found\n", stderr)
        return nil
    }
    guard status == errSecSuccess else {
        if let message = SecCopyErrorMessageString(status, nil) {
            fputs("\(CommandLine.arguments[0]): SecItemCopyMatching failed with \"\(message)\"\n", stderr)
        } else {
            fputs("\(CommandLine.arguments[0]): SecItemCopyMatching returned status \(status)\n", stderr)
        }
        return nil
    }
    
    guard let existingItem = item as? [String : Any],
        let passwordData = existingItem[kSecValueData as String] as? Data,
        let password = String(data: passwordData, encoding: String.Encoding.utf8)
    else {
        fputs("\(CommandLine.arguments[0]): no data for \((service, item))\n", stderr)
        return nil
    }

    return password
}

func put(item: Item, for service: Service) {
    // TODO: Implement
}

// TODO: Some reasonable command line interface

if let username = get(item: .username, for: .bugzilla) {
    print("bugzilla username:", username)
    print("bugzilla password:", get(item: .credential(forUsername: username), for: .bugzilla) ?? "nil")
}

if let username = get(item: .username, for: .github) {
    print("github username:", username)
    print("github credential:", get(item: .credential(forUsername: username), for: .github) ?? "nil")
}
