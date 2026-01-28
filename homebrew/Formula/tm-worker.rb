class TmWorker < Formula
  desc "TaskMessenger Worker - Distributed task processing and execution"
  homepage "https://github.com/GITHUB_REPOSITORY"
  version "1.0.0"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/GITHUB_REPOSITORY/releases/download/v1.0.0/tm-worker-v1.0.0-macos-arm64.tar.gz"
      sha256 "PLACEHOLDER_ARM64_SHA256"
    else
      url "https://github.com/GITHUB_REPOSITORY/releases/download/v1.0.0/tm-worker-v1.0.0-macos-x86_64.tar.gz"
      sha256 "PLACEHOLDER_X86_64_SHA256"
    end
  end

  depends_on :macos

  def install
    # Install binary
    bin.install "bin/tm-worker"
    
    # Install shared library
    lib.install "lib/libzt.dylib"
    
    # Install config template
    (etc/"task-messenger").install "config/config-worker.json"
    
    # Install documentation
    doc.install Dir["doc/*"] if Dir.exist?("doc")
    doc.install "LICENSE" if File.exist?("LICENSE")
  end

  def post_install
    # Create user config directory
    config_dir = "#{Dir.home}/Library/Application Support/TaskMessenger/config/worker"
    FileUtils.mkdir_p(config_dir)
    
    # Copy config template if doesn't exist
    config_file = "#{config_dir}/config-worker.json"
    unless File.exist?(config_file)
      FileUtils.cp("#{etc}/task-messenger/config-worker.json", config_file)
      ohai "Created config at: #{config_file}"
    end
  end

  def caveats
    <<~EOS
      TaskMessenger Worker has been installed!
      
      Configuration file:
        ~/Library/Application Support/TaskMessenger/config/worker/config-worker.json
      
      To start the worker:
        tm-worker -c "~/Library/Application Support/TaskMessenger/config/worker/config-worker.json"
      
      Or use the full path to the config file.
    EOS
  end

  test do
    system "#{bin}/tm-worker", "--version"
  end
end
