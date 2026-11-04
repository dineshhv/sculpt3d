import Foundation

/// Evaluates arithmetic over numbers and named variables:
/// `+ - * / ^`, parentheses, unary minus, and `sqrt sin cos tan abs min max`
/// (trigonometry in degrees). Returns nil for anything it can't parse.
struct ExpressionEvaluator {
    var variables: [String: Double]

    /// Resolves project variables in order, so later ones can use earlier ones.
    init(_ list: [Variable]) {
        variables = [:]
        for v in list {
            if let value = ExpressionEvaluator(values: variables).evaluate(v.expression) {
                variables[v.name.lowercased()] = value
            }
        }
    }

    init(values: [String: Double]) {
        variables = values
    }

    func evaluate(_ text: String) -> Double? {
        var parser = Parser(tokens: Self.tokenize(text), variables: variables)
        guard let value = parser.expression(), parser.atEnd, value.isFinite else { return nil }
        return value
    }

    /// True when the text is a plain number, not an expression.
    static func isNumber(_ text: String) -> Bool {
        Double(text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")) != nil
    }

    private enum Token: Equatable {
        case number(Double), name(String), op(Character)
    }

    private static func tokenize(_ text: String) -> [Token] {
        var tokens: [Token] = []
        var chars = Array(text), i = 0
        while i < chars.count {
            let c = chars[i]
            if c.isWhitespace {
                i += 1
            } else if c.isNumber || c == "." {
                var j = i
                while j < chars.count, chars[j].isNumber || chars[j] == "." { j += 1 }
                tokens.append(.number(Double(String(chars[i..<j])) ?? .nan))
                i = j
            } else if c.isLetter || c == "_" {
                var j = i
                while j < chars.count, chars[j].isLetter || chars[j].isNumber || chars[j] == "_" { j += 1 }
                tokens.append(.name(String(chars[i..<j]).lowercased()))
                i = j
            } else {
                tokens.append(.op(c))
                i += 1
            }
        }
        chars.removeAll()
        return tokens
    }

    private struct Parser {
        let tokens: [Token]
        let variables: [String: Double]
        var i = 0

        var atEnd: Bool { i == tokens.count }

        init(tokens: [Token], variables: [String: Double]) {
            self.tokens = tokens
            self.variables = variables
        }

        mutating func expression() -> Double? {
            guard var value = term() else { return nil }
            while i < tokens.count, case let .op(o) = tokens[i], o == "+" || o == "-" {
                i += 1
                guard let rhs = term() else { return nil }
                value = o == "+" ? value + rhs : value - rhs
            }
            return value
        }

        mutating func term() -> Double? {
            guard var value = power() else { return nil }
            while i < tokens.count, case let .op(o) = tokens[i], o == "*" || o == "/" {
                i += 1
                guard let rhs = power() else { return nil }
                value = o == "*" ? value * rhs : value / rhs
            }
            return value
        }

        mutating func power() -> Double? {
            guard let base = unary() else { return nil }
            if i < tokens.count, tokens[i] == .op("^") {
                i += 1
                guard let exponent = power() else { return nil }
                return pow(base, exponent)
            }
            return base
        }

        mutating func unary() -> Double? {
            if i < tokens.count, tokens[i] == .op("-") {
                i += 1
                return unary().map { -$0 }
            }
            if i < tokens.count, tokens[i] == .op("+") { i += 1 }
            return primary()
        }

        mutating func primary() -> Double? {
            guard i < tokens.count else { return nil }
            switch tokens[i] {
            case let .number(v):
                i += 1
                return v
            case .op("("):
                i += 1
                guard let v = expression(), i < tokens.count, tokens[i] == .op(")") else { return nil }
                i += 1
                return v
            case let .name(n):
                i += 1
                if i < tokens.count, tokens[i] == .op("(") {
                    i += 1
                    var args: [Double] = []
                    while true {
                        guard let a = expression() else { return nil }
                        args.append(a)
                        guard i < tokens.count, case let .op(o) = tokens[i] else { return nil }
                        i += 1
                        if o == ")" { break }
                        if o != "," && o != ";" { return nil }
                    }
                    return call(n, args)
                }
                if n == "pi" { return .pi }
                return variables[n]
            default:
                return nil
            }
        }

        func call(_ name: String, _ a: [Double]) -> Double? {
            let rad = Double.pi / 180
            switch (name, a.count) {
            case ("sqrt", 1): return sqrt(a[0])
            case ("abs", 1): return abs(a[0])
            case ("sin", 1): return sin(a[0] * rad)
            case ("cos", 1): return cos(a[0] * rad)
            case ("tan", 1): return tan(a[0] * rad)
            case ("min", 2...): return a.min()
            case ("max", 2...): return a.max()
            default: return nil
            }
        }
    }
}
