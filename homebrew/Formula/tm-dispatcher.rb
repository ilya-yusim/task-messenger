class TmDispatcher < Formula
  desc "TaskMessenger Dispatcher - Distributed task coordination and dispatch"
  homepage "https://github.com/ilya-yusim/task-messenger"
  version "0.0.1"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/ilya-yusim/task-messenger/releases/download/vTest/tm-dispatcher-vTest-macos-arm64.tar.gz"
      sha256 ""
    else
      url "https://github.com/ilya-yusim/task-messenger/releases/download/vTest/tm-dispatcher-vTest-macos-x86_64.tar.gz"
      sha256 "PLACEHOLDER_X86_64_SHA256"
    end
  end

  depends_on :macos

  def install
    # Install binary
    bin.install "bin/tm-dispatcher"
    
    # Install shared library
    lib.install "lib/libzt.dylib"
    
    # Install config templates
    (etc/"task-messenger").install "config/config-dispatcher.json"
    (etc/"task-messenger").install "config/vn-rendezvous-identity"
    
    # Install documentation
    doc.install Dir["doc/*"] if Dir.exist?("doc")
    doc.install "LICENSE" if File.exist?("LICENSE")
  end

  def post_install
    # Create user config directory
    config_dir = "#{Dir.home}/Library/Application Support/TaskMessenger/config/dispatcher"
    FileUtils.mkdir_p(config_dir)
    
    # Copy config template if doesn't exist
    config_file = "#{config_dir}/config-dispatcher.json"
    unless File.exist?(config_file)
      FileUtils.cp("#{etc}/task-messenger/config-dispatcher.json", config_file)
      ohai "Created config at: #{config_file}"
    end
    
    # Copy identity directory if doesn't exist
    identity_dir = "#{config_dir}/vn-rendezvous-identity"
    unless Dir.exist?(identity_dir)
      FileUtils.cp_r("#{etc}/task-messenger/vn-rendezvous-identity", identity_dir)
      # Set restrictive permissions on secret file
      FileUtils.chmod(0600, "#{identity_dir}/identity.secret")
      ohai "Created identity at: #{identity_dir}"
    end
  end

  def caveats
    <<~EOS
      TaskMessenger Dispatcher has been installed!
      
      Configuration file:
        ~/Library/Application Support/TaskMessenger/config/dispatcher/config-dispatcher.json
      
      Identity files:
        ~/Library/Application Support/TaskMessenger/config/dispatcher/vn-rendezvous-identity/
      
      To start the dispatcher:
        tm-dispatcher -c "~/Library/Application Support/TaskMessenger/config/dispatcher/config-dispatcher.json"
      
      Or use the full path to the config file.
    EOS
  end

  test do
    system "#{bin}/tm-dispatcher", "--version"
  end
end
